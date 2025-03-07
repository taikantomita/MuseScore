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

#ifndef __SYMBOL_H__
#define __SYMBOL_H__

#include <memory>

#include "modularity/ioc.h"
#include "draw/types/font.h"

#include "iengravingfontsprovider.h"

#include "bsymbol.h"

namespace mu::engraving {
class Segment;
class IEngravingFont;

//---------------------------------------------------------
//   @@ Symbol
///    Symbol constructed from builtin symbol.
//
//   @P symbol       string       the SMuFL name of the symbol
//---------------------------------------------------------

class Symbol : public BSymbol
{
    OBJECT_ALLOCATOR(engraving, Symbol)

    INJECT(engraving, IEngravingFontsProvider, engravingFonts)
protected:
    SymId _sym;
    std::shared_ptr<IEngravingFont> _scoreFont = nullptr;

public:
    Symbol(const ElementType& type, EngravingItem* parent, ElementFlags f = ElementFlag::MOVABLE);
    Symbol(EngravingItem* parent, ElementFlags f = ElementFlag::MOVABLE);
    Symbol(const Symbol&);

    Symbol& operator=(const Symbol&) = delete;

    Symbol* clone() const override { return new Symbol(*this); }

    void setSym(SymId s, const std::shared_ptr<IEngravingFont>& sf = nullptr) { _sym  = s; _scoreFont = sf; }
    SymId sym() const { return _sym; }
    mu::AsciiStringView symName() const;

    String accessibleInfo() const override;

    void draw(mu::draw::Painter*) const override;
    void write(XmlWriter& xml) const override;
    void read(XmlReader&) override;
    void layout() override;

    PropertyValue getProperty(Pid) const override;
    bool setProperty(Pid, const PropertyValue&) override;

    double baseLine() const override { return 0.0; }
    virtual Segment* segment() const { return (Segment*)explicitParent(); }
};

//---------------------------------------------------------
//   @@ FSymbol
///    Symbol constructed from a font glyph (i.e. a text character or emoji).
//---------------------------------------------------------

class FSymbol final : public BSymbol
{
    OBJECT_ALLOCATOR(engraving, FSymbol)

    mu::draw::Font _font;
    int _code; // character code point (Unicode)

public:
    FSymbol(EngravingItem* parent);
    FSymbol(const FSymbol&);

    FSymbol* clone() const override { return new FSymbol(*this); }

    String toString() const;
    String accessibleInfo() const override;

    void draw(mu::draw::Painter*) const override;
    void write(XmlWriter& xml) const override;
    void read(XmlReader&) override;
    void layout() override;

    double baseLine() const override { return 0.0; }
    Segment* segment() const { return (Segment*)explicitParent(); }
    mu::draw::Font font() const { return _font; }
    int code() const { return _code; }
    void setFont(const mu::draw::Font& f);
    void setCode(int val) { _code = val; }
};
} // namespace mu::engraving
#endif
