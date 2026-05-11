#include "ProgressBar.h"

#include <cmath>

namespace stemmerizer::ui
{

ProgressBar::ProgressBar()
{
    startTimerHz (60);
}

ProgressBar::~ProgressBar() = default;

void ProgressBar::setProgress (float f)
{
    indeterminate = (f < 0.f);
    if (! indeterminate) target = juce::jlimit (0.f, 1.f, f);
}

void ProgressBar::timerCallback()
{
    shown += (target - shown) * 0.18f;
    phase += 0.02f;
    if (phase > 1.f) phase -= 1.f;
    repaint();
}

void ProgressBar::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const float h = juce::jmin (r.getHeight(), 6.f);
    juce::Rectangle<float> track (r.getX(), r.getCentreY() - h * 0.5f, r.getWidth(), h);

    g.setColour (Theme::col (Theme::kBorderStrong));
    g.fillRoundedRectangle (track, h * 0.5f);

    if (indeterminate)
    {
        const float w  = track.getWidth() * 0.30f;
        const float x  = track.getX() + std::fmod (phase * (track.getWidth() + w), track.getWidth() + w) - w;
        g.setColour (Theme::col (Theme::kAccent));
        g.fillRoundedRectangle ({ x, track.getY(), w, track.getHeight() }, h * 0.5f);
    }
    else
    {
        const float w = track.getWidth() * shown;
        if (w > 0.f)
        {
            g.setColour (Theme::col (Theme::kAccent));
            g.fillRoundedRectangle ({ track.getX(), track.getY(), w, track.getHeight() }, h * 0.5f);
        }
    }
}

} // namespace stemmerizer::ui
