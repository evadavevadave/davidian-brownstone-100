/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/* VST3 / Audio Unit GUI (DPF). It shows the same house-style panel as the LV2 GUI:
 * panel.h draws into a pixel buffer, which is uploaded as an OpenGL texture and drawn
 * scaled to the window, and mouse events are passed back to panel.h. */

/* System headers first, so the panel's includes inside the namespace below are no-ops. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace bs {
#include "ui/panel.h"
}

#include "DistrhoUI.hpp"
#include "OpenGL.hpp"

#ifndef GL_BGRA
#    define GL_BGRA 0x80E1
#endif
#ifndef GL_CLAMP_TO_EDGE
#    define GL_CLAMP_TO_EDGE 0x812F
#endif

START_NAMESPACE_DISTRHO

static const uint kDoubleClickMs = 350;

class BrownstoneUI : public UI
{
public:
    BrownstoneUI()
        : UI(PANEL_W, PANEL_H, true),
          fTexture(0),
          fUpload(true),
          fEditing(-1),
          fLastClick(0),
          fLastX(-100.0f),
          fLastY(-100.0f),
          fReady(false)
    {
        fReady = bs::panel_init(&fPanel, writeCallback, this) != 0;
    }

    ~BrownstoneUI() override
    {
        if (fTexture != 0)
            glDeleteTextures(1, &fTexture);
        if (fReady)
            bs::panel_free(&fPanel);
    }

protected:
    void parameterChanged(uint32_t index, float value) override
    {
        if (!fReady)
            return;
        bs::panel_set(&fPanel, (int)index + 2, value);
        repaintIfDirty();
    }

    void uiIdle() override
    {
        repaintIfDirty();
    }

    void onDisplay() override
    {
        if (!fReady)
            return;
        if (fPanel.dirty)
        {
            bs::panel_draw(&fPanel);
            fUpload = true;
        }

        glEnable(GL_TEXTURE_2D);
        if (fTexture == 0)
        {
            glGenTextures(1, &fTexture);
            glBindTexture(GL_TEXTURE_2D, fTexture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            /* 0x00RRGGBB words are B,G,R,X bytes in memory; the unused byte is ignored. */
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, PANEL_W, PANEL_H, 0, GL_BGRA, GL_UNSIGNED_BYTE,
                         fPanel.px);
            fUpload = false;
        }
        else
        {
            glBindTexture(GL_TEXTURE_2D, fTexture);
            if (fUpload)
            {
                glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, PANEL_W, PANEL_H, GL_BGRA, GL_UNSIGNED_BYTE,
                                fPanel.px);
                fUpload = false;
            }
        }

        const double w = getWidth(), h = getHeight();
        glDisable(GL_BLEND);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex2d(0.0, 0.0);
        glTexCoord2f(1.0f, 0.0f); glVertex2d(w, 0.0);
        glTexCoord2f(1.0f, 1.0f); glVertex2d(w, h);
        glTexCoord2f(0.0f, 1.0f); glVertex2d(0.0, h);
        glEnd();
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (!fReady || ev.button != 1)
            return false;
        const float x = toPanelX(ev.pos.getX()), y = toPanelY(ev.pos.getY());
        if (ev.press)
        {
            const bool dbl = ev.time - fLastClick < kDoubleClickMs
                             && fabsf(x - fLastX) < 4.0f && fabsf(y - fLastY) < 4.0f;
            fLastClick = dbl ? 0 : ev.time;
            fLastX = x;
            fLastY = y;
            beginEdit(bs::hit(x, y));
            bs::panel_mouse_down(&fPanel, x, y, dbl ? 1 : 0);
            /* a click that doesn't start a drag (switch, footswitch, list) is a whole edit */
            if (fPanel.drag < 0)
                endEdit();
        }
        else
        {
            bs::panel_mouse_up(&fPanel);
            endEdit();
        }
        repaintIfDirty();
        return true;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (!fReady || fPanel.drag < 0)
            return false;
        bs::panel_mouse_move(&fPanel, toPanelX(ev.pos.getX()), toPanelY(ev.pos.getY()),
                             (ev.mod & DGL_NAMESPACE::kModifierShift) != 0);
        repaintIfDirty();
        return true;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        if (!fReady)
            return false;
        const float x = toPanelX(ev.pos.getX()), y = toPanelY(ev.pos.getY());
        const double d = ev.delta.getY();
        if (d == 0.0)
            return false;
        beginEdit(bs::hit(x, y));
        bs::panel_wheel(&fPanel, x, y, d > 0.0 ? 1.0f : -1.0f, (ev.mod & DGL_NAMESPACE::kModifierShift) != 0);
        endEdit();
        repaintIfDirty();
        return true;
    }

private:
    static void writeCallback(void* host, int port, float value)
    {
        BrownstoneUI* const self = static_cast<BrownstoneUI*>(host);
        if (port >= 2)
            self->setParameterValue((uint32_t)(port - 2), value);
    }

    void beginEdit(int widget)
    {
        endEdit();
        if (widget >= 0)
        {
            fEditing = bs::WIDGETS[widget].port - 2;
            editParameter((uint32_t)fEditing, true);
        }
    }

    void endEdit()
    {
        if (fEditing >= 0)
        {
            editParameter((uint32_t)fEditing, false);
            fEditing = -1;
        }
    }

    float toPanelX(double x) const { return (float)(x * PANEL_W / (double)getWidth()); }
    float toPanelY(double y) const { return (float)(y * PANEL_H / (double)getHeight()); }

    void repaintIfDirty()
    {
        if (fPanel.dirty)
            repaint();
    }

    bs::Panel fPanel;
    GLuint fTexture;
    bool fUpload;
    int fEditing;
    uint fLastClick;
    float fLastX, fLastY;
    bool fReady;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BrownstoneUI)
};

UI* createUI()
{
    return new BrownstoneUI();
}

END_NAMESPACE_DISTRHO
