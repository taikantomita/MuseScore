/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore BVBA and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "instrchange.h"

#include "translation.h"
#include "rw/xml.h"

#include "keysig.h"
#include "measure.h"
#include "mscore.h"
#include "part.h"
#include "score.h"
#include "segment.h"
#include "staff.h"
#include "undo.h"

using namespace mu;

namespace mu::engraving {
//---------------------------------------------------------
//   instrumentChangeStyle
//---------------------------------------------------------

static const ElementStyle instrumentChangeStyle {
    { Sid::instrumentChangePlacement,          Pid::PLACEMENT },
    { Sid::instrumentChangeMinDistance,        Pid::MIN_DISTANCE },
};

//---------------------------------------------------------
//   InstrumentChange
//---------------------------------------------------------

InstrumentChange::InstrumentChange(EngravingItem* parent)
    : TextBase(ElementType::INSTRUMENT_CHANGE, parent, TextStyleType::INSTRUMENT_CHANGE, ElementFlag::MOVABLE | ElementFlag::ON_STAFF)
{
    initElementStyle(&instrumentChangeStyle);
    _instrument = new Instrument();
}

InstrumentChange::InstrumentChange(const Instrument& i, EngravingItem* parent)
    : TextBase(ElementType::INSTRUMENT_CHANGE, parent, TextStyleType::INSTRUMENT_CHANGE, ElementFlag::MOVABLE | ElementFlag::ON_STAFF)
{
    initElementStyle(&instrumentChangeStyle);
    _instrument = new Instrument(i);
}

InstrumentChange::InstrumentChange(const InstrumentChange& is)
    : TextBase(is)
{
    _instrument = new Instrument(*is._instrument);
    _init = is._init;
}

InstrumentChange::~InstrumentChange()
{
    delete _instrument;
}

void InstrumentChange::setInstrument(const Instrument& i)
{
    *_instrument = i;
    //delete _instrument;
    //_instrument = new Instrument(i);
}

void InstrumentChange::setupInstrument(const Instrument* instrument)
{
    if (_init) {
        Fraction tickStart = segment()->tick();
        Part* part = staff()->part();
        Interval oldV = part->instrument(tickStart)->transpose();
        bool concPitch = score()->styleB(Sid::concertPitch);

        // change the clef for each staff
        for (size_t i = 0; i < part->nstaves(); i++) {
            ClefType oldClefType = concPitch ? part->instrument(tickStart)->clefType(i)._concertClef
                                   : part->instrument(tickStart)->clefType(i)._transposingClef;
            ClefType newClefType = concPitch ? instrument->clefType(i)._concertClef
                                   : instrument->clefType(i)._transposingClef;
            // Introduce cleff change only if the new clef *symbol* is different from the old one
            if (ClefInfo::symId(oldClefType) != ClefInfo::symId(newClefType)) {
                // If instrument change is at the start of a measure, use the measure as the element, as this will place the instrument change before the barline.
                EngravingItem* element = rtick().isZero() ? toEngravingItem(findMeasure()) : toEngravingItem(this);
                score()->undoChangeClef(part->staff(i), element, newClefType, true);
            }
        }

        // Change key signature if necessary. CAUTION: not necessary in case of octave-transposing!
        if ((instrument->transpose().chromatic - oldV.chromatic) % 12) {
            for (size_t i = 0; i < part->nstaves(); i++) {
                if (!part->staff(i)->keySigEvent(tickStart).isAtonal()) {
                    KeySigEvent ks;
                    ks.setForInstrumentChange(true);
                    Key key = part->staff(i)->key(tickStart);
                    if (!score()->styleB(Sid::concertPitch)) {
                        key = transposeKey(key, oldV);
                    }
                    ks.setKey(key);
                    score()->undoChangeKeySig(part->staff(i), tickStart, ks);
                }
            }
        }

        // change instrument in all linked scores
        for (EngravingObject* se : linkList()) {
            InstrumentChange* lic = static_cast<InstrumentChange*>(se);
            Instrument* newInstrument = new Instrument(*instrument);
            lic->score()->undo(new ChangeInstrument(lic, newInstrument));
        }

        // transpose for current score only
        // this automatically propagates to linked scores
        if (part->instrument(tickStart)->transpose() != oldV) {
            auto i = part->instruments().upper_bound(tickStart.ticks());          // find(), ++i
            Fraction tickEnd;
            if (i == part->instruments().end()) {
                tickEnd = Fraction(-1, 1);
            } else {
                tickEnd = Fraction::fromTicks(i->first);
            }
            score()->transpositionChanged(part, oldV, tickStart, tickEnd);
        }

        //: The text of an "instrument change" marking. It is an instruction to the player to switch to another instrument.
        const String newInstrChangeText = mtrc("engraving", "To %1").arg(instrument->trackName());
        undoChangeProperty(Pid::TEXT, TextBase::plainToXmlText(newInstrChangeText));
    }
}

//---------------------------------------------------------
//   keySigs
//---------------------------------------------------------

std::vector<KeySig*> InstrumentChange::keySigs() const
{
    std::vector<KeySig*> keysigs;
    Segment* seg = segment()->prev1(SegmentType::KeySig);
    if (seg) {
        voice_idx_t startVoice = part()->staff(0)->idx() * VOICES;
        voice_idx_t endVoice = part()->staff(part()->nstaves() - 1)->idx() * VOICES;
        Fraction t = tick();
        for (voice_idx_t i = startVoice; i <= endVoice; i += VOICES) {
            KeySig* ks = toKeySig(seg->element(i));
            if (ks && ks->forInstrumentChange() && ks->tick() == t) {
                keysigs.push_back(ks);
            }
        }
    }
    return keysigs;
}

//---------------------------------------------------------
//   clefs
//---------------------------------------------------------

std::vector<Clef*> InstrumentChange::clefs() const
{
    std::vector<Clef*> clefs;
    Segment* seg = segment()->prev1(SegmentType::Clef);
    if (seg) {
        voice_idx_t startVoice = part()->staff(0)->idx() * VOICES;
        voice_idx_t endVoice = part()->staff(part()->nstaves() - 1)->idx() * VOICES;
        Fraction t = tick();
        for (voice_idx_t i = startVoice; i <= endVoice; i += VOICES) {
            Clef* clef = toClef(seg->element(i));
            if (clef && clef->forInstrumentChange() && clef->tick() == t) {
                clefs.push_back(clef);
            }
        }
    }
    return clefs;
}

//---------------------------------------------------------
//   write
//---------------------------------------------------------

void InstrumentChange::write(XmlWriter& xml) const
{
    xml.startElement(this);
    _instrument->write(xml, part());
    if (_init) {
        xml.tag("init", _init);
    }
    TextBase::writeProperties(xml);
    xml.endElement();
}

//---------------------------------------------------------
//   read
//---------------------------------------------------------

void InstrumentChange::read(XmlReader& e)
{
    while (e.readNextStartElement()) {
        const AsciiStringView tag(e.name());
        if (tag == "Instrument") {
            _instrument->read(e, part());
        } else if (tag == "init") {
            _init = e.readBool();
        } else if (!TextBase::readProperties(e)) {
            e.unknown();
        }
    }
    if (score()->mscVersion() < 206) {
        // previous versions did not honor transposition of instrument change
        // except in ways that it should not have
        // notes entered before the instrument change was added would not be altered,
        // so original transposition remained in effect
        // notes added afterwards would be transposed by both intervals, resulting in tpc corruption
        // here we set the instrument change to inherit the staff transposition to emulate previous versions
        // in Note::read(), we attempt to fix the tpc corruption
        // There is also code in read206 to try to deal with this, but it is out of date and therefore disabled
        // What this means is, scores created in 2.1 or later should be fine, scores created in 2.0 maybe not so much

        Interval v = staff() ? staff()->part()->instrument(tick())->transpose() : 0;
        _instrument->setTranspose(v);
    }
}

//---------------------------------------------------------
//   propertyDefault
//---------------------------------------------------------

engraving::PropertyValue InstrumentChange::propertyDefault(Pid propertyId) const
{
    switch (propertyId) {
    case Pid::TEXT_STYLE:
        return TextStyleType::INSTRUMENT_CHANGE;
    default:
        return TextBase::propertyDefault(propertyId);
    }
}

//---------------------------------------------------------
//   layout
//---------------------------------------------------------

void InstrumentChange::layout()
{
    TextBase::layout();
    autoplaceSegmentElement();
}
}
