#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace ui
{
namespace col
{
    inline const juce::Colour bg0 { 0xff0b0b0c }, bg1 { 0xff111112 }, bg2 { 0xff181819 }, line { 0xff2a2a2d },
                              dim { 0xff5a5a60 }, text { 0xffd9d9dc }, bright { 0xffffffff },
                              active { 0xff5cf2a8 }, accent { 0xff6f9bff }, warn { 0xffff6b5c };
}

inline juce::Font font (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

// Texto "espaçado" (tipografia premium): "STRENGTH" -> "S T R E N G T H"
inline juce::String spaced (const juce::String& s)
{
    juce::String o;
    for (int i = 0; i < s.length(); ++i) { o << s[i]; if (i < s.length() - 1) o << " "; }
    return o;
}

// Look and feel mínimo: todos os componentes do Strata se desenham sozinhos; isto só garante que
// menus/popups e tooltips do host herdem a paleta (nada de visual padrão do JUCE).
class CustomLookAndFeel : public juce::LookAndFeel_V4
{
public:
    CustomLookAndFeel()
    {
        setColour (juce::PopupMenu::backgroundColourId, col::bg1);
        setColour (juce::PopupMenu::textColourId, col::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, col::bg2);
        setColour (juce::TooltipWindow::backgroundColourId, col::bg1);
        setColour (juce::TooltipWindow::textColourId, col::text);
        setColour (juce::ResizableWindow::backgroundColourId, col::bg0);
    }
};
} // namespace ui
