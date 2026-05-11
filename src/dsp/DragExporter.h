#pragma once

#include "StemSession.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <string>
#include <vector>

namespace stemmerizer::dsp
{

/// Renders one or more stems to temp WAV files on disk and triggers an
/// OS-level drag-and-drop so the user can drag them straight into a DAW
/// timeline.
///
/// The temp files live in `<temp dir>/Stemmerizer/<session id>/` and are
/// cleaned up on the next process start (we don't unlink immediately
/// because the DAW may still be reading them after the drop).
class DragExporter
{
public:
    /// Drag a single stem out. Returns false if the stem couldn't be
    /// rendered. `sourceComponent` is the JUCE component that initiated
    /// the drag (used for the OS drag session).
    static bool dragStem (juce::Component*               sourceComponent,
                          const StemSession::Snapshot&   snap,
                          int                            stemIndex,
                          const std::string&             sourceBasename);

    /// Drag every stem out at once.
    static bool dragAllStems (juce::Component*             sourceComponent,
                              const StemSession::Snapshot& snap,
                              const std::string&           sourceBasename);

    /// Drag the current mix (with mix state applied) out.
    static bool dragMixdown (juce::Component*             sourceComponent,
                             const StemSession::Snapshot& snap,
                             const StemMixState&          mix,
                             const std::string&           sourceBasename);

    /// Resolved temp dir for this session, e.g.
    /// %TEMP%\Stemmerizer\<pid>\.
    static juce::File scratchDir();

    /// Best-effort cleanup of stale scratch dirs from prior runs.
    static void purgeOldScratchDirs();
};

} // namespace stemmerizer::dsp
