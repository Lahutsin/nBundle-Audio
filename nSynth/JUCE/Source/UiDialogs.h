#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace nSynth::ui
{
// Runs `action` right away when `ask` is false, otherwise after the confirm button of an async
// warning box (no modal loop). `box` keeps the dialog: destroying or reassigning it closes the
// box without running the action, so `action` may capture its owner.
inline void confirmThen (bool ask, juce::ScopedMessageBox& box, juce::Component* associatedComponent,
                         const juce::String& title, const juce::String& message, const juce::String& confirmText,
                         std::function<void()> action)
{
    if (! ask)
    {
        action();
        return;
    }
    box = juce::AlertWindow::showScopedAsync (juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::WarningIcon,
                                                                                            title, message, confirmText, "Cancel",
                                                                                            associatedComponent),
                                              [action = std::move (action)] (int result)
                                              {
                                                  if (result == 1)
                                                      action();
                                              });
}
} // namespace nSynth::ui
