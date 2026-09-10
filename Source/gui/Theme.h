/*
    BrownSweep - Perceptual High-Pass
    Copyright (c) Yoav Audio

    Theme.h - the whole colour scheme in one place.  Dark, warm, and deliberately
    limited: one accent colour for anything the user controls, one dim colour for
    reference information, and nothing else.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace bsgui::theme
{

inline const juce::Colour background   { 0xff121315 };
inline const juce::Colour panel        { 0xff1a1b1e };
inline const juce::Colour panelDeep    { 0xff0d0e10 };
inline const juce::Colour outline      { 0xff2c2e33 };
inline const juce::Colour outlineBright{ 0xff3d4047 };
inline const juce::Colour text         { 0xff8e9199 };
inline const juce::Colour textBright   { 0xffd6d8dc };
inline const juce::Colour accent       { 0xffd8853a };
inline const juce::Colour accentDim    { 0xff70491f };
inline const juce::Colour reference    { 0xff4d545e };
inline const juce::Colour knobBody     { 0xff232529 };
inline const juce::Colour knobBodyDark { 0xff141517 };

inline juce::Font labelFont (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultSansSerifFontName(), height,
                                          bold ? juce::Font::bold : juce::Font::plain));
}

inline juce::Font monoFont (float height)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height, juce::Font::plain));
}

} // namespace bsgui::theme
