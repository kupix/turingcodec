/*
Copyright (C) 2016 British Broadcasting Corporation, Parabola Research
and Queen Mary University of London.

This file is part of the Turing codec.

The Turing codec is free software; you can redistribute it and/or modify
it under the terms of version 2 of the GNU General Public License as
published by the Free Software Foundation.

The Turing codec is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

Commercial support and intellectual property rights for
the Turing codec are also available under a proprietary license.
For more information, contact us at info @ turingcodec.org.
 */

#include "InputQueue.h"
#include "HevcTypes.h"
#include <cassert>
#include <deque>

struct LookaheadAnalysisResults
{
};


namespace {

struct Piece
{
    Piece(std::shared_ptr<PictureWrapper> picture)
        :
        picture(picture)
    {
    }

    std::shared_ptr<InputQueue::Docket> docket;
    std::shared_ptr<PictureWrapper> picture;
    std::shared_ptr<AdaptiveQuantisation> aqInfo;
    std::shared_ptr<LookaheadAnalysisResults> lookaheadAnalysisResults;

    bool done() const
    {
        return !this->picture;
    }
};

void addReference(InputQueue::Docket &docket, int ref)
{
    if (ref < 0) docket.references.negative.insert(ref);
    if (ref > 0) docket.references.positive.insert(ref);
}

}


struct InputQueue::State
{
    State(int maxGopN, int maxGopM, bool fieldCoding, bool shotChange)
        :
        maxGopN(maxGopN),
        maxGopM(maxGopM),
        fieldCoding(fieldCoding),
        shotChange(shotChange)
    {
    }

    const int maxGopN;
    const int maxGopM;
    const bool fieldCoding;
    const bool shotChange;


    // list of pictures in preanalysis stage
    std::deque<Piece> entriesPreanalysis;

    // list of pictures use during SOP planning - ~8 pictures
    std::deque<Piece> entries;

    struct Timestamp
    {
        int64_t timestamp;
        size_t sequence;
    };

    std::deque<Timestamp> timestamps;

    size_t pictureInputCount = 0;
    int sequenceFront = 0;
    int sequenceIdr = 0;
    bool finish = false;
    int gopSize;

    void process();

    void createDocket(int size, int i, int nut, int qpOffset, double qpFactor, int ref1 = 0, int ref2 = 0, int ref3 = 0, int ref4 = 0);

    bool isValidReference(int i, int delta) const
    {
        assert(i > 0);
        if (delta < 0) return true;
#ifdef FORCE_P_SLICES
        return false;
#endif
        if (delta == 0) return false;
        int offset = sequenceIdr - sequenceFront + 1;
        int deltaLimit = (static_cast<int>(this->entries.size() > offset)) ? offset : static_cast<int>(this->entries.size());
        if (i - 1 + delta >= deltaLimit) return false;
        return !!this->entries.at(i - 1 + delta).docket;
    }

    std::vector<int>        m_shotChangeList;
    void setShotChangeList(std::vector<int>& shotChangeList) { if (shotChangeList.size()) m_shotChangeList.swap(shotChangeList); };
    int computeNextIdr(int sequenceFront, int nextDefaultIdr, bool fieldCoding = false)
    {
        int nextIdr = nextDefaultIdr;
        int scale = fieldCoding ? 1 : 0;
        for (int i = sequenceFront; i < nextDefaultIdr; i++)
        {
            int index = (i >> scale);
            index = (index < 0) ? 0 : index;
            if (index >= m_shotChangeList.size())
                break;

            if (i % (fieldCoding + 1) == 0 && m_shotChangeList[index])
            {
                nextIdr = i;
                break;
            }
        }
        return nextIdr;
    }
};


InputQueue::InputQueue(int maxGopN, int maxGopM, bool fieldCoding, bool shotChange)
    :
    state(new State(maxGopN, maxGopM, fieldCoding, shotChange))
{
}


InputQueue::~InputQueue() = default;


void InputQueue::State::createDocket(int max, int i, int nut, int qpOffset, double qpFactor, int ref1, int ref2, int ref3, int ref4)
{
    assert(i > 0);
    if (i <= max)
    {
        Docket *docket = new Docket();
        docket->poc = this->sequenceFront + i - 1;
        docket->nut = nut;
        docket->qpOffset = qpOffset;
        docket->qpFactor = qpFactor;
        docket->currentGopSize = gopSize;

        if (this->isValidReference(i, ref1)) addReference(*docket, ref1);
        if (this->isValidReference(i, ref2)) addReference(*docket, ref2);
        if (this->isValidReference(i, ref3)) addReference(*docket, ref3);
        if (this->isValidReference(i, ref4)) addReference(*docket, ref4);

        docket->sliceType = isIrap(nut) ? I : B;
        this->entries.at(i - 1).docket.reset(docket);
    }
}


void InputQueue::State::process()
{
    if (this->entries.empty() || this->entries.front().docket) return;

    if (this->shotChange)
    {
        if ((this->sequenceIdr - this->sequenceFront) < 0)
            this->sequenceIdr = computeNextIdr(this->sequenceFront, (this->sequenceIdr + this->maxGopN), this->fieldCoding);
    }
    else
    {
        if (this->sequenceIdr - this->sequenceFront < 0)
            this->sequenceIdr += this->maxGopN;
    }

    gopSize = this->maxGopM;

    if (gopSize != 1 && gopSize != 8) 
        throw std::runtime_error("max-gop-m must be either 1 or 8"); // review: still the case?

    char lastPicture = 'P';

    if (this->finish && int(entries.size()) < gopSize)
    {
        gopSize = int(entries.size());
        lastPicture = 0;
    }

    const int gopSizeIdr = this->sequenceIdr - this->sequenceFront + 1;
    if (gopSizeIdr <= gopSize)
    {
        gopSize = gopSizeIdr;
        lastPicture = 'I';
    }

    if (int(entries.size()) < gopSize) 
        return;

    int max = gopSize;

    auto nutR = TRAIL_R;
    auto nutN = TRAIL_N;

    if (lastPicture == 'I')
    {
        int nut = this->sequenceFront ? CRA_NUT : IDR_N_LP;
        this->createDocket(gopSize, gopSize, nut, 0, 0.4420, -gopSize);
        max = gopSize - 1;
        nutR = RASL_R;
        nutN = RASL_N;
    }
    else if (lastPicture == 'P')
    {
        this->createDocket(gopSize, gopSize, TRAIL_R, 1, 0.4420, -gopSize, -gopSize);
        max = gopSize - 1;
    }

    if (!this->finish && gopSize != 8)
    {
        if (gopSize == 2)
        {
            this->createDocket(max, 1, nutR, 2, 0.6800, -1, 1);
        }
        else if (gopSize == 3)
        {
            this->createDocket(max, 2, nutR, 2, 0.3536, -2, 1);
            this->createDocket(max, 1, nutN, 3, 0.6800, -1, 2, 1);
        }
        else if (gopSize == 4)
        {
            this->createDocket(max, 2, nutR, 2, 0.3536, -2, 2);
            this->createDocket(max, 1, nutN, 3, 0.6800, -1, 3, 1);
            this->createDocket(max, 3, nutN, 3, 0.6800, -1, 1);
        }
        else if (gopSize == 5)
        {
            this->createDocket(max, 3, nutR, 2, 0.3536, -3, 2);
            this->createDocket(max, 1, nutR, 2, 0.3536, -1, 4, 2);
            this->createDocket(max, 2, nutN, 3, 0.6800, -2, 3, -1, 1);
            this->createDocket(max, 4, nutN, 3, 0.6800, -4, 1, -1);
        }
        else if (gopSize == 6)
        {
            this->createDocket(max, 3, nutR, 2, 0.3536, -3, 3);
            this->createDocket(max, 1, nutR, 3, 0.3536, -1, 5, 2);
            this->createDocket(max, 2, nutN, 4, 0.6800, -2, 4, 1, -1);
            this->createDocket(max, 5, nutR, 3, 0.3536, -5, 1, -2);
            this->createDocket(max, 4, nutN, 4, 0.6800, -4, 2, -1, 1);
        }
        else if (gopSize == 7)
        {
            this->createDocket(max, 4, nutR, 2, 0.3536, -4, 3);
            this->createDocket(max, 2, nutR, 3, 0.3536, -2, 5, 2);
            this->createDocket(max, 1, nutN, 4, 0.6800, -1, 6, 3, 1);
            this->createDocket(max, 3, nutN, 4, 0.6800, -3, 4, 1, -1);
            this->createDocket(max, 6, nutR, 3, 0.3536, -2, 1);
            this->createDocket(max, 5, nutN, 4, 0.6800, -1, 2, 1);
        }
    }
    else
    {
        this->createDocket(max, 4, nutR, 2, 0.3536, -4, 4);
        this->createDocket(max, 2, nutR, 3, 0.3536, -2, 2, 6);
        this->createDocket(max, 1, nutN, 4, 0.6800, -1, 1, 3, 7);
        this->createDocket(max, 3, nutN, 4, 0.6800, -1, 1, -3, 5);
        this->createDocket(max, 6, nutR, 3, 0.3536, -2, 2, -6);
        this->createDocket(max, 5, nutN, 4, 0.6800, -1, 1, 3, -5);
        this->createDocket(max, 7, nutN, 4, 0.6800, -1, 1, -7);
    }
}


void InputQueue::append(std::shared_ptr<PictureWrapper> picture, std::shared_ptr<AdaptiveQuantisation> aqInfo)
{
    assert(!this->state->finish);
    this->state->entriesPreanalysis.push_back(Piece(picture));
    this->state->entriesPreanalysis.back().aqInfo = aqInfo;

    auto &timestamps = this->state->timestamps;

    size_t const reorderDelay = 3; // review: could reduce - this relates to *_max_num_reorder_pics

    if (this->state->pictureInputCount == 1)
    {
        // upon the second picture, we know the PTS period
        auto const period = picture->pts - timestamps.front().timestamp;
        for (size_t i=0; i<reorderDelay; ++i)
            timestamps.push_front({ timestamps.front().timestamp - period, i });
    }
    timestamps.push_back({ picture->pts, this->state->pictureInputCount + reorderDelay });
    ++this->state->pictureInputCount;
}


void InputQueue::endOfInput()
{
    this->state->finish = true;
}

void InputQueue::preanalyse()
{
	auto n = this->state->entriesPreanalysis.size();
	if (n >= 10 || this->state->finish)
	{
		if (n > 10)
			n = 10;

		// do preanalysis here
		int sum = 0;
		for (int i = 0; i < n; ++i)
		{
			auto picture = this->state->entriesPreanalysis.at(i).picture;
			if (picture->sampleSize == 8)
			{
				auto &pictureWrap = static_cast<PictureWrap<uint8_t> &>(*picture);
				auto &orgPicture = static_cast<Picture<uint8_t> &>(pictureWrap);
				sum += orgPicture[0](0, 0);
			}
			else
			{

			}
			
		}

		if (n)
			std::cout << "sum over " << n << " pictures is " << sum << "\n";

		for (int i = 0; i < n; ++i)
		{
			this->state->entries.push_back(this->state->entriesPreanalysis.front());
			this->state->entriesPreanalysis.pop_front();
		}
	}
}


bool InputQueue::eos() const
{
    return this->state->finish;
}


std::shared_ptr<InputQueue::Docket> InputQueue::getDocket()
{
    if (this->state->pictureInputCount == 1 && !this->eos())
        return nullptr;

    this->state->setShotChangeList(m_shotChangeList);
    this->state->process();

    int isIntraFrame = -1;
    bool encodeIntraFrame = false;
    int size = int(this->state->entries.size() > 8) ? 8 : int(this->state->entries.size());
    for (int i = 0; i < size; ++i)
    {
        if (!this->state->entries.at(i).docket || this->state->entries.at(i).done()) break;
        if (this->state->entries.at(i).docket->sliceType == I)
        {
            isIntraFrame = i;
            encodeIntraFrame = true;
        }
    }

    std::shared_ptr<InputQueue::Docket> docket;
    for (int i = 0; i < int(this->state->entries.size()); ++i)
    {
        docket = this->state->entries.at(i).docket;
        if (!docket) 
            break;

        bool canEncode = true;

        for (int deltaPoc : docket->references.positive)
            if (!this->state->entries.at(i + deltaPoc).done()) 
                canEncode = false;

        if (encodeIntraFrame && i != isIntraFrame)
            canEncode = false;

        if (encodeIntraFrame && i == isIntraFrame)
            canEncode = true;

        if (canEncode)
        {
            docket->picture = this->state->entries.at(i).picture;
            docket->aqInfo = this->state->entries.at(i).aqInfo;
            this->state->entries.at(i).picture = 0;
            break;
        }
    }

    while (!this->state->entries.empty() && this->state->entries.front().done())
    {
        this->state->entries.pop_front();
        ++this->state->sequenceFront;
    }

    if (docket)
    {
        docket->dts = this->state->timestamps.front().timestamp;
        this->state->timestamps.pop_front();
    }

    return docket;
}
