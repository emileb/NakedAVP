#ifndef touch_interface_h
#define touch_interface_h

#define NO_SEC

#include "touch_interface_base.h"

class TouchInterface : public TouchInterfaceBase
{
public:
    void createControls(std::string filesPath);

    void openGLEnd();

    void openGLStart();

    void blankButton(int state, int code);

    void newFrame();

    void automapButton(int state, int code);

    void newGLContext();

    void mouseMove(int action, float x, float y, float mouse_x, float mouse_y);

    // Overridden to cover all three species screens, not just the active one.
    bool saveControlSettings(std::string path);

    bool loadControlSettings(std::string path);

private:
    // One full game screen per species; tcGameMain points at the active one.
    touchcontrols::TouchControls *tcGameMarine = NULL;
    touchcontrols::TouchControls *tcGamePredator = NULL;
    touchcontrols::TouchControls *tcGameAlien = NULL;

    void addBaseGameControls(touchcontrols::TouchControls *tc);

    void addMarineControls(touchcontrols::TouchControls *tc);

    void addPredatorControls(touchcontrols::TouchControls *tc);

    void addAlienControls(touchcontrols::TouchControls *tc);

    void updateSpeciesControls();

    void setActiveSpecies(touchcontrols::TouchControls *tc);

    void loadSpeciesXML(touchcontrols::TouchControls *tc, std::string file);

    int lastPlayerType = -1;
    unsigned int lastAbilities = 0;
};

#endif /* touch_interface_h */
