#include "TestHelpers.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace nsynth;
using namespace nsynth::test;

class EditorTests : public juce::UnitTest
{
public:
    EditorTests() : juce::UnitTest ("Editor", "nSynth") {}

    // Counts gesture begin / end calls of one parameter.
    struct GestureCounter final : public juce::AudioProcessorParameter::Listener
    {
        void parameterValueChanged (int, float) override {}
        void parameterGestureChanged (int, bool starting) override { (starting ? begins : ends)++; }
        int begins { 0 }, ends { 0 };
    };

    template <typename T>
    static T* findChild (juce::Component& c, std::function<bool (T&)> predicate)
    {
        if (auto* t = dynamic_cast<T*> (&c); t != nullptr && predicate (*t))
            return t;
        for (auto* child : c.getChildren())
            if (auto* found = findChild<T> (*child, predicate))
                return found;
        return nullptr;
    }

    static ParamControl* findControl (juce::Component& c, const juce::String& id)
    {
        return findChild<ParamControl> (c, [&id] (ParamControl& p) { return p.getParamId() == id; });
    }

    static juce::File writeFile (const juce::File& file, const juce::String& text)
    {
        file.getParentDirectory().createDirectory();
        file.replaceWithText (text);
        return file;
    }

    void runTest() override
    {
        beginTest ("param control: radio segment buttons, MIDI CC caption and tooltip, macro rename without the CC suffix");
        {
            NSynthAudioProcessor p;
            const auto& specs = getLayerParamSpecs();
            const auto& macroSpec = specs[(size_t) findLayerSpecIndex (pid::macro1)];
            ParamControl control (p.getState(), layerParamId (Layer::upper, pid::macro1), macroSpec, juce::Colours::white);
            int cc = -1;
            juce::String macroName ("Space");
            control.setMidiLearnHandlers ([] { return true; }, [&cc] { cc = -1; }, [&cc] { return cc; });
            control.setEditableCaption ([&macroName] { return macroName; }, [&macroName] (const juce::String& n) { macroName = n; });
            auto* caption = findChild<juce::Label> (control, [] (juce::Label& l) { return l.isEditableOnDoubleClick(); });
            expect (caption != nullptr);

            control.refresh (1);
            expectEquals (caption->getText(), juce::String ("Space"));
            cc = 21;
            control.refresh (1);   // same generation: the CC is not looked up again
            expectEquals (caption->getText(), juce::String ("Space"));
            control.refresh (2);
            expectEquals (caption->getText(), juce::String ("Space [CC 21]"));
            expect (control.getTooltip().contains ("MIDI CC 21"));

            // Renaming edits only the name.
            caption->showEditor();
            if (auto* editor = caption->getCurrentTextEditor())
            {
                expectEquals (editor->getText(), juce::String ("Space"));
                editor->setText ("Wide", false);
            }
            caption->hideEditor (false);
            expectEquals (macroName, juce::String ("Wide"));
            expectEquals (caption->getText(), juce::String ("Wide [CC 21]"));

            const auto& modeSpec = specs[(size_t) findLayerSpecIndex (pid::env1Mode)];
            ParamControl segments (p.getState(), layerParamId (Layer::upper, pid::env1Mode), modeSpec, juce::Colours::white);
            expectEquals (segments.getChoiceButtonCount(), 3);
            for (auto* child : segments.getChildren())
                if (auto* button = dynamic_cast<juce::TextButton*> (child))
                    expectEquals (button->getRadioGroupId(), ParamControl::kChoiceRadioGroup);
        }

        beginTest ("param control: arrow and page keys step a knob inside a gesture; the ribbon springs back in one");
        {
            NSynthAudioProcessor p;
            const auto id = layerParamId (Layer::upper, pid::lpf);
            const auto& spec = getLayerParamSpecs()[(size_t) findLayerSpecIndex (pid::lpf)];
            ParamControl control (p.getState(), id, spec, juce::Colours::white);
            auto* parameter = p.getState().getParameter (id);
            GestureCounter gestures;
            parameter->addListener (&gestures);
            parameter->setValueNotifyingHost (0.5f);
            expect (control.getWantsKeyboardFocus());
            expect (control.keyPressed (juce::KeyPress (juce::KeyPress::upKey)));
            expectWithinAbsoluteError (parameter->getValue(), 0.51f, 1.0e-3f);
            expect (control.keyPressed (juce::KeyPress (juce::KeyPress::pageDownKey)));
            expectWithinAbsoluteError (parameter->getValue(), 0.41f, 1.0e-3f);
            expect (! control.keyPressed (juce::KeyPress (juce::KeyPress::upKey, juce::ModifierKeys::commandModifier, 0)));
            expectEquals (gestures.begins, 2);
            expectEquals (gestures.ends, 2);
            parameter->removeListener (&gestures);

            const auto ribbonId = perfParamId (pid::ribbon);
            const nsynth::ParamSpec* ribbonSpec = nullptr;
            for (auto& s : getPerformanceParamSpecs())
                if (juce::String (s.suffix) == pid::ribbon)
                    ribbonSpec = &s;
            expect (ribbonSpec != nullptr);
            ParamControl ribbon (p.getState(), ribbonId, *ribbonSpec, juce::Colours::white);
            auto* ribbonParameter = p.getState().getParameter (ribbonId);
            ribbonParameter->setValueNotifyingHost (0.9f);
            GestureCounter ribbonGestures;
            ribbonParameter->addListener (&ribbonGestures);
            ribbon.getSlider()->onDragEnd();
            expectWithinAbsoluteError (p.getState().getRawParameterValue (ribbonId)->load(), 0.0f, 1.0e-4f);
            expectEquals (ribbonGestures.begins, 1);
            expectEquals (ribbonGestures.ends, 1);
            ribbonParameter->removeListener (&ribbonGestures);
        }

        beginTest ("editor: chorus power is one gesture; undo / redo buttons and shortcuts; performance dirty marker");
        {
            NSynthAudioProcessor p;
            std::unique_ptr<NSynthAudioProcessorEditor> ed (dynamic_cast<NSynthAudioProcessorEditor*> (p.createEditor()));

            auto* chorus = p.getState().getParameter (layerParamId (Layer::upper, pid::chorus));
            GestureCounter gestures;
            chorus->addListener (&gestures);
            auto& card = ed->getFxPage (Layer::upper).getCard (FxModule::chorus);
            auto* power = findChild<juce::ToggleButton> (card, [] (juce::ToggleButton& b) { return b.getTooltip().startsWith ("Chorus on/off"); });
            expect (power != nullptr);
            power->onClick();
            expectEquals (gestures.begins, 1);
            expectEquals (gestures.ends, 1);
            chorus->removeListener (&gestures);

            // Undo returns to the state before the edit; Cmd/Ctrl+Shift+Z redoes it.
            const auto id = layerParamId (Layer::upper, pid::lpf);
            const float before = p.getState().getRawParameterValue (id)->load();
            setParam (p.getState(), id, 1000.0f);
            p.captureUndoPoint();
            p.captureUndoPoint();
            ed->undo();
            expectWithinAbsoluteError (p.getState().getRawParameterValue (id)->load(), before, 0.5f);
            expect (ed->getRedoButton().isEnabled());
            expect (ed->keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)));
            expectWithinAbsoluteError (p.getState().getRawParameterValue (id)->load(), 1000.0f, 0.5f);
            expect (ed->getUndoButton().isEnabled() && ! ed->getRedoButton().isEnabled());
            expect (ed->keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)));
            expectWithinAbsoluteError (p.getState().getRawParameterValue (id)->load(), before, 0.5f);
            expect (ed->keyPressed (juce::KeyPress ('y', juce::ModifierKeys::commandModifier, 0)));
            expectWithinAbsoluteError (p.getState().getRawParameterValue (id)->load(), 1000.0f, 0.5f);
            expect (! ed->keyPressed (juce::KeyPress ('z', 0, 0)));

            auto& browser = ed->getPresetBrowser();
            browser.setKind (nSynth::PresetKind::performance);
            browser.onClosed();
            expect (ed->getPresetDisplay().getPresetName().endsWith (" *"));
            ed.reset();
        }

        beginTest ("editor: INIT asks first when the layer has unsaved edits");
        {
            NSynthAudioProcessor p;
            std::unique_ptr<NSynthAudioProcessorEditor> ed (dynamic_cast<NSynthAudioProcessorEditor*> (p.createEditor()));
            auto* init = findChild<juce::TextButton> (ed->getContent(), [] (juce::TextButton& b) { return b.getButtonText() == "INIT"; });
            expect (init != nullptr);
            const auto id = layerParamId (Layer::upper, pid::lpf);
            setParam (p.getState(), id, 1000.0f);
            init->onClick();   // dirty: a dialog opens, nothing is reset yet
            expectWithinAbsoluteError (p.getState().getRawParameterValue (id)->load(), 1000.0f, 0.5f);
            p.initLayer (Layer::upper);
            init->onClick();   // clean: runs right away
            expect (! p.isPatchDirty (Layer::upper));
            setParam (p.getState(), id, 1000.0f);
            init->onClick();
            ed.reset();   // closes the open dialog without running it
        }

        beginTest ("preset browser: closed, the arrows step through the loaded preset's category without the search");
        {
            NSynthAudioProcessor p;
            std::unique_ptr<NSynthAudioProcessorEditor> ed (dynamic_cast<NSynthAudioProcessorEditor*> (p.createEditor()));
            auto& browser = ed->getPresetBrowser();
            auto& library = p.getPresetLibrary();
            ed->openPresetBrowser (nSynth::PresetKind::patch);
            browser.setCategory ("Bass");
            expect (browser.loadRow (2, false));
            browser.setCategory ("Pads");
            browser.setSearchText ("no such preset at all");
            browser.close();
            browser.step (1);
            const auto bass = library.filter (nSynth::PresetKind::patch, "Bass", {});
            expectEquals (p.getPatchKey (Layer::upper), library.getKey (bass[3]));
            browser.step (-2);
            expectEquals (p.getPatchKey (Layer::upper), library.getKey (bass[1]));
            ed.reset();
        }

        beginTest ("favourites: user presets stored relative to the user folder, old absolute keys still read, instances merge");
        {
            const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("nsynth_favourites", "");
            const auto userPatch = writeFile (root.getChildFile ("Patches").getChildFile ("My Pad.nsynthpatch"), "<NSYNTH_PATCH/>");
            nSynth::PresetLibrary a (root), b (root);
            a.rescan();
            b.rescan();
            const auto* user = a.findByKey (nSynth::PresetEntry { nSynth::PresetKind::patch, "My Pad", "User", false, userPatch }.key());
            expect (user != nullptr);
            if (user != nullptr)
            {
                expectEquals (a.favouriteKey (*user), juce::String ("user/patch/Patches/My Pad.nsynthpatch"));
                expect (a.setFavourite (*user, true));
                expect (root.getChildFile ("Favourites.xml").loadFileAsString().contains ("user/patch/Patches/My Pad.nsynthpatch"));
                expect (! root.getChildFile ("Favourites.xml").loadFileAsString().contains (root.getFullPathName()));

                // A second instance adds its favourite on top of the first one's.
                const auto& factory = b.getEntries().front();
                expect (b.setFavourite (factory, true));
                nSynth::PresetLibrary c (root);
                c.rescan();
                expect (c.isFavourite (factory));
                expect (c.isFavourite (*c.findByKey (user->key())));
                expectEquals (c.count (nSynth::PresetKind::patch, nSynth::PresetLibrary::favouritesCategory), 2);
            }

            // Version 1 favourites: absolute paths.
            juce::XmlElement legacy ("FAVOURITES");
            legacy.createNewChildElement ("ITEM")->setAttribute ("key", "user/patch/" + userPatch.getFullPathName());
            expect (legacy.writeTo (root.getChildFile ("Favourites.xml")));
            nSynth::PresetLibrary d (root);
            d.rescan();
            const auto* legacyUser = d.findByKey (nSynth::PresetEntry { nSynth::PresetKind::patch, "My Pad", "User", false, userPatch }.key());
            expect (legacyUser != nullptr && d.isFavourite (*legacyUser));
            root.deleteRecursively();
        }
    }
};

static EditorTests editorTests;
