//
// AVP's own copy of the touch control layout, adapted from the sibling
// :Unreal module's. UE1-specific controls (dodge buttons, translator/datapad)
// are dropped; the rest of the layout carries over unchanged.
//

#include "touch_interface.h"
#include "avp_touch_input.h"
#include "SDL3/SDL_keycode.h"
#include "SDL3/SDL_scancode.h"

// Editor cycle group shared by all the in-game screens.
#define GAME_EDIT_GROUP 1

void TouchInterface::openGLStart()
{
    touchcontrols::gl_startRender();
};

void TouchInterface::openGLEnd()
{
    touchcontrols::gl_endRender();

    // Engine shares this gl4es instance, drop our program and flush the batch.
    touchcontrols::gl_resetGL4ES();
};

void TouchInterface::mouseMove(int action, float x, float y, float mouse_x, float mouse_y)
{
}

//
// The controls every species shares. Called once per species screen, so each
// gets its own instances - nothing is shared between the three.
//
void TouchInterface::addBaseGameControls(touchcontrols::TouchControls *tc)
{
    tc->setAlpha(touchSettings.alpha);
    tc->addControl(new touchcontrols::Button("back", touchcontrols::RectF(0, 0, 2, 2), "back_button", KEY_BACK_BUTTON, false, false, "Show menu"));
    tc->addControl(new touchcontrols::Button("attack", touchcontrols::RectF(20, 7, 23, 10), "shoot", KEY_SHOOT, false, false, "Attack!"));
    tc->addControl(new touchcontrols::Button("attack2", touchcontrols::RectF(3, 5, 6, 8), "shoot", KEY_SHOOT, false, true, "Attack! (duplicate)"));

    tc->addControl(new touchcontrols::Button("use", touchcontrols::RectF(23, 6, 26, 9), "use", PORT_ACT_USE, false, false, "Use/Open"));
    tc->addControl(new touchcontrols::Button("quick_save", touchcontrols::RectF(24, 0, 26, 2), "save", PORT_ACT_QUICKSAVE, false, false, "Quick save"));
    tc->addControl(new touchcontrols::Button("quick_load", touchcontrols::RectF(20, 0, 22, 2), "load", PORT_ACT_QUICKLOAD, false, false, "Quick load"));

    tc->addControl(new touchcontrols::Button("keyboard", touchcontrols::RectF(8, 0, 10, 2), "keyboard", KEY_SHOW_KBRD, false, false, "Show keyboard"));
    tc->addControl(new touchcontrols::Button("show_mouse", touchcontrols::RectF(4, 0, 6, 2), "left_mouse", KEY_USE_MOUSE, false, true, "Use mouse"));

    tc->addControl(new touchcontrols::Button("jump", touchcontrols::RectF(24, 3, 26, 5), "jump", PORT_ACT_JUMP, false, false, "Jump"));

    // Toggle only: crouch is a sustained state here, and for the Alien it is
    // the wall-crawl mode, so there is no point holding a button down for it.
    tc->addControl(new touchcontrols::Button("crouch_toggle", touchcontrols::RectF(24, 14, 26, 16), "crouch", PORT_ACT_TOGGLE_CROUCH, false, false, "Crouch (toggle)"));
    tc->addControl(new touchcontrols::Button("attack_alt", touchcontrols::RectF(21, 5, 23, 7), "shoot_alt", PORT_ACT_ALT_ATTACK, false, true, "Alt fire"));
    tc->addControl(new touchcontrols::Button("show_custom", touchcontrols::RectF(0, 7, 2, 9), "custom_show", KEY_SHOW_CUSTOM, false, true, "Show custom"));
    tc->addControl(new touchcontrols::Button("show_weapons", touchcontrols::RectF(12, 14, 14, 16), "show_weapons", KEY_SHOW_WEAPONS, false, false, "Show numbers"));
    tc->addControl(new touchcontrols::Button("next_weapon", touchcontrols::RectF(0, 3, 3, 5), "next_weap", PORT_ACT_NEXT_WEP, false, false, "Next weapon"));
    tc->addControl(new touchcontrols::Button("prev_weapon", touchcontrols::RectF(0, 5, 3, 7), "prev_weap", PORT_ACT_PREV_WEP, false, false, "Prev weapon"));
    tc->addControl(new touchcontrols::Button("console", touchcontrols::RectF(6, 0, 8, 2), "tild", PORT_ACT_CONSOLE, false, true, "Console"));

    touchcontrols::ButtonGrid *dpad = new touchcontrols::ButtonGrid("dpad_move", touchcontrols::RectF(6, 3, 12, 7), "", 3, 2, true, "Movement btns (WASD)");

    dpad->addCell(0, 1, "direction_left", PORT_ACT_MOVE_LEFT);
    dpad->addCell(2, 1, "direction_right", PORT_ACT_MOVE_RIGHT);
    dpad->addCell(1, 0, "direction_up", PORT_ACT_FWD);
    dpad->addCell(1, 1, "direction_down", PORT_ACT_BACK);
    tc->addControl(dpad);

    touchcontrols::TouchJoy *right = new touchcontrols::TouchJoy("touch", touchcontrols::RectF(17, 4, 26, 16), "look_arrow", "fixed_stick_circle");
    tc->addControl(right);
    right->signal_move.connect(sigc::mem_fun(this, &TouchInterface::rightStick));
    right->signal_double_tap.connect(sigc::mem_fun(this, &TouchInterface::rightDoubleTap));

    touchcontrols::TouchJoy *left = new touchcontrols::TouchJoy("stick", touchcontrols::RectF(0, 7, 8, 16), "strafe_arrow", "fixed_stick_circle");
    tc->addControl(left);
    left->signal_move.connect(sigc::mem_fun(this, &TouchInterface::leftStick));
    left->signal_double_tap.connect(sigc::mem_fun(this, &TouchInterface::leftDoubleTap));

    // SWAPFIX
    left->registerTouchJoySWAPFIX(right);
    right->registerTouchJoySWAPFIX(left);

    tc->signal_button.connect(sigc::mem_fun(this, &TouchInterface::gameButton));
    tc->signal_settingsButton.connect(sigc::mem_fun(this, &TouchInterface::gameSettingsButton));
}

void TouchInterface::addMarineControls(touchcontrols::TouchControls *tc)
{
    tc->addControl(new touchcontrols::Button("vision", touchcontrols::RectF(16, 3, 18, 5), "goggles", PORT_ACT_AVP_VISION, false, false, "Image intensifier"));
    tc->addControl(new touchcontrols::Button("flare", touchcontrols::RectF(18, 3, 20, 5), "flashlight", PORT_ACT_AVP_FLARE, false, false, "Throw flare"));
    // Unhidden by updateSpeciesControls when the level grants it.
    tc->addControl(new touchcontrols::Button("jetpack", touchcontrols::RectF(20, 3, 22, 5), "wings", PORT_ACT_AVP_JETPACK, false, true, "Jetpack"));
}

void TouchInterface::addPredatorControls(touchcontrols::TouchControls *tc)
{
    tc->addControl(new touchcontrols::Button("cloak", touchcontrols::RectF(20, 3, 22, 5), "holster", PORT_ACT_AVP_CLOAK, false, false, "Cloak"));
    tc->addControl(new touchcontrols::Button("cycle_vision", touchcontrols::RectF(16, 3, 18, 5), "goggles", PORT_ACT_AVP_CYCLE_VISION, false, false, "Cycle vision mode"));
    // Zoom is a 4-level stepped range, so slide up/down rather than two buttons.
    touchcontrols::QuadSlide *zoomQs = new touchcontrols::QuadSlide("quad_slide_zoom", touchcontrols::RectF(14, 3, 16, 5), "binocular", "slide_arrow",
                                                                   PORT_ACT_AVP_ZOOM_IN, 0, PORT_ACT_AVP_ZOOM_OUT, 0, false, "Zoom in/out");
    zoomQs->signal.connect(sigc::mem_fun(this, &TouchInterface::gameButton));
    tc->addControl(zoomQs);
    tc->addControl(new touchcontrols::Button("recall_disc", touchcontrols::RectF(18, 3, 20, 5), "reload", PORT_ACT_AVP_RECALL_DISC, false, false, "Recall disc"));
    // Unhidden by updateSpeciesControls when the level grants it.
    tc->addControl(new touchcontrols::Button("grapple", touchcontrols::RectF(22, 3, 24, 5), "force_pull", PORT_ACT_AVP_GRAPPLE, false, true, "Grappling hook"));
}

void TouchInterface::addAlienControls(touchcontrols::TouchControls *tc)
{
    tc->addControl(new touchcontrols::Button("vision", touchcontrols::RectF(16, 3, 18, 5), "goggles", PORT_ACT_AVP_VISION, false, false, "Alien sense"));
}

void TouchInterface::createControls(std::string filesPath)
{
    tcMenuMain = new touchcontrols::TouchControls("menu", false, true, 10, false);
    tcYesNo = new touchcontrols::TouchControls("yes_no", false, false);
    // Marine is the starting screen, so only it is in the editor cycle -
    // updateSpeciesControls moves that with the active species.
    tcGameMarine = new touchcontrols::TouchControls("game_marine", false, true, GAME_EDIT_GROUP, true);
    tcGamePredator = new touchcontrols::TouchControls("game_predator", false, true, -1, true);
    tcGameAlien = new touchcontrols::TouchControls("game_alien", false, true, -1, true);
    tcGameWeapons = new touchcontrols::TouchControls("weapons", false, true, 1, false);
    tcWeaponWheel = new touchcontrols::TouchControls("weapon_wheel", false, true, 1, false);
    tcBlank = new touchcontrols::TouchControls("blank", true, false);
    tcCustomButtons = new touchcontrols::TouchControls("custom_buttons", false, true, 1, true);
    tcKeyboard = new touchcontrols::TouchControls("keyboard", false, false);
    tcGamepadUtility = new touchcontrols::TouchControls("gamepad_utility", false, false);
    tcMouse = new touchcontrols::TouchControls("mouse", false, false);
    // Hide the cog because when using the gamepad and weapon wheel is enabled, the cog will show otherwise
    tcWeaponWheel->hideEditButton = true;

    //Menu -------------------------------------------
    //------------------------------------------------------
    tcMenuMain->addControl(new touchcontrols::Button("back", touchcontrols::RectF(0, 0, 2, 2), "back_button", KEY_BACK_BUTTON));
    tcMenuMain->addControl(new touchcontrols::Button("down_arrow", touchcontrols::RectF(20, 13, 23, 16), "arrow_down", PORT_ACT_MENU_DOWN));
    tcMenuMain->addControl(new touchcontrols::Button("up_arrow", touchcontrols::RectF(20, 10, 23, 13), "arrow_up", PORT_ACT_MENU_UP));
    tcMenuMain->addControl(new touchcontrols::Button("left_arrow", touchcontrols::RectF(17, 13, 20, 16), "arrow_left", PORT_ACT_MENU_LEFT));
    tcMenuMain->addControl(new touchcontrols::Button("right_arrow", touchcontrols::RectF(23, 13, 26, 16), "arrow_right", PORT_ACT_MENU_RIGHT));
    tcMenuMain->addControl(new touchcontrols::Button("enter", touchcontrols::RectF(0, 10, 6, 16), "enter", PORT_ACT_MENU_SELECT));
    tcMenuMain->addControl(new touchcontrols::Button("keyboard", touchcontrols::RectF(2, 0, 4, 2), "keyboard", KEY_SHOW_KBRD));

    tcMenuMain->addControl(new touchcontrols::Button("gamepad", touchcontrols::RectF(22, 0, 24, 2), "gamepad", KEY_SHOW_GAMEPAD));
    tcMenuMain->addControl(new touchcontrols::Button("gyro", touchcontrols::RectF(24, 0, 26, 2), "gyro", KEY_SHOW_GYRO));
    tcMenuMain->addControl(new touchcontrols::Button("load_save_touch", touchcontrols::RectF(20, 0, 22, 2), "touchscreen_save", KEY_LOAD_SAVE_CONTROLS));

    touchcontrols::Mouse *brightnessSlide = new touchcontrols::Mouse("slide_mouse", touchcontrols::RectF(24, 3, 26, 11), "brightness_slider");
    brightnessSlide->signal_action.connect(sigc::mem_fun(this, &TouchInterface::brightnessSlideMouse));
    tcMenuMain->addControl(brightnessSlide);

    tcMenuMain->signal_button.connect(sigc::mem_fun(this, &TouchInterface::menuButton));
    tcMenuMain->setAlpha(0.8);
    tcMenuMain->setFixAspect(true);

    //Game -------------------------------------------
    //------------------------------------------------------
    // A full screen per species, so the abilities are laid out and edited
    // together with the controls they sit next to. tcGameMain points at the
    // active one, which is all the base class tracks.
    //
    // Abilities go in first: the look stick covers most of the right side, and
    // whichever control was added first wins the touch, so adding it before
    // them would make the ability buttons hard to grab in the editor.
    addMarineControls(tcGameMarine);
    addBaseGameControls(tcGameMarine);

    addPredatorControls(tcGamePredator);
    addBaseGameControls(tcGamePredator);

    addAlienControls(tcGameAlien);
    addBaseGameControls(tcGameAlien);

    // Marine is the starting species. This also points the base's stick
    // pointers at its screen, which is all the base ever applies settings to.
    setActiveSpecies(tcGameMarine);

    //Weapons -------------------------------------------
    //------------------------------------------------------
    tcGameWeapons->addControl(new touchcontrols::Button("weapon1", touchcontrols::RectF(1, 14, 3, 16), "key_1", 1));
    tcGameWeapons->addControl(new touchcontrols::Button("weapon2", touchcontrols::RectF(3, 14, 5, 16), "key_2", 2));
    tcGameWeapons->addControl(new touchcontrols::Button("weapon3", touchcontrols::RectF(5, 14, 7, 16), "key_3", 3));
    tcGameWeapons->addControl(new touchcontrols::Button("weapon4", touchcontrols::RectF(7, 14, 9, 16), "key_4", 4));
    tcGameWeapons->addControl(new touchcontrols::Button("weapon5", touchcontrols::RectF(9, 14, 11, 16), "key_5", 5));

    tcGameWeapons->addControl(new touchcontrols::Button("weapon6", touchcontrols::RectF(15, 14, 17, 16), "key_6", 6));
    tcGameWeapons->addControl(new touchcontrols::Button("weapon7", touchcontrols::RectF(17, 14, 19, 16), "key_7", 7));
    tcGameWeapons->addControl(new touchcontrols::Button("weapon8", touchcontrols::RectF(19, 14, 21, 16), "key_8", 8));
    tcGameWeapons->addControl(new touchcontrols::Button("weapon9", touchcontrols::RectF(21, 14, 23, 16), "key_9", 9));
    tcGameWeapons->addControl(new touchcontrols::Button("weapon0", touchcontrols::RectF(23, 14, 25, 16), "key_0", 0));

    tcGameWeapons->signal_button.connect(sigc::mem_fun(this, &TouchInterface::selectWeaponButton));
    tcGameWeapons->setAlpha(0.8);

    //Weapon wheel -------------------------------------------
    //------------------------------------------------------

    wheelSelect = new touchcontrols::WheelSelect("weapon_wheel", touchcontrols::RectF(7, 2, 19, 14), "weapon_wheel_%d", wheelNbr);
    wheelSelect->signal_selected.connect(sigc::mem_fun(this, &TouchInterface::weaponWheel));
    wheelSelect->signal_enabled.connect(sigc::mem_fun(this, &TouchInterface::weaponWheelSelected));
    tcWeaponWheel->addControl(wheelSelect);

    if(touchSettings.weaponWheelOpaque)
        tcWeaponWheel->setAlpha(0.8);
    else
        tcWeaponWheel->setAlpha(touchSettings.alpha);

    // No inventory: AvP has no such concept - weapons are direct-select and
    // there are no usable items, so tcInventory/tcDPadInventory stay NULL.

    //Blank -------------------------------------------
    //------------------------------------------------------
    tcBlank->addControl(new touchcontrols::Button("enter", touchcontrols::RectF(0, 0, 26, 16), "", 0x123));
    tcBlank->signal_button.connect(sigc::mem_fun(this, &TouchInterface::blankButton));


    //Keyboard -------------------------------------------
    //------------------------------------------------------
    uiKeyboard = new touchcontrols::UI_Keyboard("keyboard", touchcontrols::RectF(0, 8, 26, 16), "font_dual", 0, 0, 0);
    uiKeyboard->signal.connect(sigc::mem_fun(this, &TouchInterface::keyboardKeyPressed));
    tcKeyboard->addControl(uiKeyboard);
    // We want touch to pass through only where there is no keyboard
    tcKeyboard->setPassThroughTouch(touchcontrols::TouchControls::PassThrough::NO_CONTROL);

    //Yes No -------------------------------------------
    //------------------------------------------------------
    tcYesNo->addControl(new touchcontrols::Button("yes", touchcontrols::RectF(8, 12, 11, 15), "key_y", PORT_ACT_MENU_CONFIRM));
    tcYesNo->addControl(new touchcontrols::Button("no", touchcontrols::RectF(15, 12, 18, 15), "key_n", PORT_ACT_MENU_ABORT));
    tcYesNo->signal_button.connect(sigc::mem_fun(this, &TouchInterface::menuButton));
    tcYesNo->setAlpha(0.8);

    //Gamepad utility -------------------------------------------
    //------------------------------------------------------
    touchcontrols::ButtonGrid *gamepadUtils = new touchcontrols::ButtonGrid("gamepad_grid", touchcontrols::RectF(8, 5, 18, 11), "gamepad_utils_bg", 3, 2);

    gamepadUtils->addCell(0, 0, "back_button", KEY_BACK_BUTTON);
    gamepadUtils->addCell(1, 0, "keyboard", KEY_SHOW_KBRD);
    gamepadUtils->addCell(1, 1, "tild", PORT_ACT_CONSOLE);
    gamepadUtils->addCell(2, 0, "save", PORT_ACT_QUICKSAVE);
    gamepadUtils->addCell(2, 1, "load", PORT_ACT_QUICKLOAD);

    gamepadUtils->signal_outside.connect(sigc::mem_fun(this, &TouchInterface::gameUtilitiesOutside));

    tcGamepadUtility->addControl(gamepadUtils);
    tcGamepadUtility->setAlpha(0.9);
    tcGamepadUtility->signal_button.connect(sigc::mem_fun(this, &TouchInterface::gameUtilitiesButton));

    // Relative-drag mouse look (menus / touch pointer)
    touchcontrols::Mouse *mouse = new touchcontrols::Mouse("mouse", touchcontrols::RectF(0, 0, 26, 16), "");
    mouse->setHideGraphics(true);
    mouse->setEditable(false);
    tcMouse->addControl(mouse);
    mouse->signal_action.connect(sigc::mem_fun(this, &TouchInterface::mouseMove));
    tcMouse->addControl(new touchcontrols::Button("back", touchcontrols::RectF(0, 0, 2, 2), "back_button", KEY_BACK_BUTTON, false, false, "Back"));
    tcMouse->addControl(new touchcontrols::Button("left_button", touchcontrols::RectF(0, 6, 3, 10), "left_mouse", KEY_LEFT_MOUSE, false, false, "Back"));
    tcMouse->signal_button.connect(sigc::mem_fun(this, &TouchInterface::mouseButton));

    std::string
    newSettings = (std::string) filesPath + "/touch_settings_"
    ENGINE_NAME
    ".xml";

    // Enable the Mouse Look setting for Doom engine games
    touchcontrols::tTouchSettingsModifier modifier;
    modifier.mouseLookVisible = true;

    UI_tc = touchcontrols::createDefaultSettingsUI(&controlsContainer, newSettings, &modifier);
    UI_tc->setAlpha(1);

    //---------------------------------------------------------------
    //---------------------------------------------------------------
    controlsContainer.addControlGroup(tcKeyboard);
    controlsContainer.addControlGroup(tcGamepadUtility); // before gamemain so touches don't go through
    controlsContainer.addControlGroup(tcCustomButtons);
    controlsContainer.addControlGroup(tcGameMarine);
    controlsContainer.addControlGroup(tcGamePredator);
    controlsContainer.addControlGroup(tcGameAlien);
    controlsContainer.addControlGroup(tcYesNo);
    controlsContainer.addControlGroup(tcGameWeapons);
    controlsContainer.addControlGroup(tcMenuMain);
    controlsContainer.addControlGroup(tcWeaponWheel);
    controlsContainer.addControlGroup(tcBlank);
    controlsContainer.addControlGroup(tcMouse);

    tcMenuMain->setXMLFile((std::string) filesPath + "/menu.xml");

    tcGameMarine->setXMLFile((std::string) filesPath + "/game_marine_"
    ENGINE_NAME
    ".xml");
    tcGamePredator->setXMLFile((std::string) filesPath + "/game_predator_"
    ENGINE_NAME
    ".xml");
    tcGameAlien->setXMLFile((std::string) filesPath + "/game_alien_"
    ENGINE_NAME
    ".xml");
    tcWeaponWheel->setXMLFile((std::string) filesPath + "/weaponwheel_"
    ENGINE_NAME
    ".xml");
    tcGameWeapons->setXMLFile((std::string) filesPath + "/weapons_"
    ENGINE_NAME
    ".xml");
    tcCustomButtons->setXMLFile((std::string) filesPath + "/custom_buttons_0_"
    ENGINE_NAME
    ".xml");
}

//
// Makes one species' screen the active one. The base only ever applies its
// settings to tcGameMain and to the two stick pointers, so everything it would
// have done at startup or on a settings change has to be redone here.
//
void TouchInterface::setActiveSpecies(touchcontrols::TouchControls *tc)
{
    tcGameMain = tc;

    tcGameMain->setAlpha(touchSettings.alpha);
    tcGameMain->setColour(touchSettings.defaultColor);

    touchJoyLeft = (touchcontrols::TouchJoy *) tcGameMain->getControl("stick");
    touchJoyRight = (touchcontrols::TouchJoy *) tcGameMain->getControl("touch");

    if (touchJoyLeft)
    {
        touchJoyLeft->setCenterAnchor(touchSettings.fixedMoveStick);
        touchJoyLeft->setHideGraphics(!touchSettings.showLeftStick);
    }

    if (touchJoyRight)
        touchJoyRight->setHideGraphics(!touchSettings.showRightStick);
}

//
// One save file per species screen, since the base only knows about tcGameMain.
// A preset saved before these existed just has no species files, and the base's
// tcGameMain.xml load stands.
//
bool TouchInterface::saveControlSettings(std::string path)
{
    TouchInterfaceBase::saveControlSettings(path);

    tcGameMarine->saveXML(path + "/tcGameMarine.xml");
    tcGamePredator->saveXML(path + "/tcGamePredator.xml");
    tcGameAlien->saveXML(path + "/tcGameAlien.xml");

    return false;
}

bool TouchInterface::loadControlSettings(std::string path)
{
    TouchInterfaceBase::loadControlSettings(path);

    loadSpeciesXML(tcGameMarine, path + "/tcGameMarine.xml");
    loadSpeciesXML(tcGamePredator, path + "/tcGamePredator.xml");
    loadSpeciesXML(tcGameAlien, path + "/tcGameAlien.xml");

    return false;
}

void TouchInterface::loadSpeciesXML(touchcontrols::TouchControls *tc, std::string file)
{
    tc->loadXML(file);
    tc->save(); // Save the newly loaded
}

//
// Points tcGameMain at the current species' screen. Everything else - fading,
// alpha, hiding for menus - is the base class's job and keeps working because
// it only ever looks at tcGameMain.
//
void TouchInterface::updateSpeciesControls()
{
    int playerType = AVP_GetPlayerType();
    unsigned int abilities = AVP_GetPlayerAbilities();

    if (playerType != lastPlayerType)
    {
        touchcontrols::TouchControls *next = tcGameMarine;

        if (playerType == AVP_PLAYER_PREDATOR)
            next = tcGamePredator;
        else if (playerType == AVP_PLAYER_ALIEN)
            next = tcGameAlien;

        // The editor cycles every group with the same editGroup whether it is
        // enabled or not, so drop the other two species out of the cycle.
        tcGameMarine->editGroup = (next == tcGameMarine) ? GAME_EDIT_GROUP : -1;
        tcGamePredator->editGroup = (next == tcGamePredator) ? GAME_EDIT_GROUP : -1;
        tcGameAlien->editGroup = (next == tcGameAlien) ? GAME_EDIT_GROUP : -1;

        if (next != tcGameMain)
        {
            bool wasEnabled = tcGameMain->isEnabled();

            tcGameMarine->setEnabled(false);
            tcGamePredator->setEnabled(false);
            tcGameAlien->setEnabled(false);

            setActiveSpecies(next);
            tcGameMain->setEnabled(wasEnabled);
        }

        lastPlayerType = playerType;
    }

    if (abilities != lastAbilities)
    {
        // Edge triggered, so a player who hides these again is left alone.
        touchcontrols::Button *jetpack = (touchcontrols::Button *) tcGameMarine->getControl("jetpack");
        touchcontrols::Button *grapple = (touchcontrols::Button *) tcGamePredator->getControl("grapple");

        if (jetpack)
            jetpack->setHidden(!(abilities & AVP_ABILITY_JETPACK));

        if (grapple)
            grapple->setHidden(!(abilities & AVP_ABILITY_GRAPPLE));

        lastAbilities = abilities;
    }
}

void TouchInterface::blankButton(int state, int code)
{
    PortableKeyEvent(state, SDL_SCANCODE_SPACE, 0);
}

void TouchInterface::automapButton(int state, int code)
{

}

void TouchInterface::newFrame()
{
    touchscreemode_t screenMode = PortableGetScreenMode();

    // Hack to show custom buttons while in the menu to bind keys
    if(screenMode == TS_MENU && showCustomMenu == true)
    {
        screenMode = TS_CUSTOM;
    }

    if((screenMode == TS_MAP) && (mapState == 1))
    {
        screenMode = TS_GAME;
    }

    updateTouchScreenModeOut(screenMode);
    updateTouchScreenModeIn(screenMode);

    currentScreenMode = screenMode;

    updateSpeciesControls();
}

void TouchInterface::newGLContext()
{

}
