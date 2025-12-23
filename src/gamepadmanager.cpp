/*
 * Copyright (c) 9.11.2016 com2m GmbH.
 * All rights reserved.
 */

#include "headers/gamepadmanager.h"
#include <unistd.h>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>

GamepadManager::GamepadManager(int count, QObject* parent) : QThread(parent) {
    (void)count;
}

QList<Joystick*> GamepadManager::getGamepads() {
    return gamepadList;
}

Joystick* GamepadManager::getGamepad(int index) {
    if (index >= 0 && index < gamepadList.length()) {
        return gamepadList.at(index);
    } else {
        return 0;
    }
}

Joystick* GamepadManager::getGamepad(Racecar* racecar) {
    foreach (Joystick* gamepad, gamepadList) {
        if (gamepad->getRacecar() == racecar) {
            return gamepad;
        }
    }

    return 0;
}

void GamepadManager::addGamepad(Joystick* gamepad) {
    gamepadList.append(gamepad);
}

int GamepadManager::attachAvailableGamepads(const QList<Racecar*>& racecars) {
    // Remove old gamepads (if any)
    foreach (Joystick* gp, gamepadList) {
        delete gp;
    }
    gamepadList.clear();

    QDir dir("/dev/input");
    const QStringList entries = dir.entryList(QStringList() << "js*", QDir::System | QDir::Files, QDir::Name);

    int attached = 0;

    const int limit = qMin(entries.size(), racecars.size());
    for (int i = 0; i < limit; ++i) {
        const QString dev = dir.absoluteFilePath(entries.at(i));
        Joystick* gp = new Joystick(dev.toStdString());
        gp->setRacecar(racecars.at(i));

        if (!gp->isFound()) {
            qWarning().noquote() << "[" + racecars.at(i)->getAddress().toString() + "]>> GAMEPAD OPEN FAILED: " + dev;
            delete gp;
            continue;
        }

        gamepadList.append(gp);
        attached++;

        qDebug().noquote() << "[" + racecars.at(i)->getAddress().toString() + "]>> GAMEPAD ATTACHED: " + dev;
    }

    return attached;
}

Joystick* GamepadManager::addGamepad(Racecar *racecar) {
    // Backwards compatible: create a joystick for the next js index.
    Joystick* gamepad = new Joystick(gamepadList.size());
    gamepad->setRacecar(racecar);

    gamepadList.append(gamepad);

    return gamepad;
}

void GamepadManager::removeGamepad(Joystick* gamepad) {
    gamepadList.removeAll(gamepad);
}

void GamepadManager::removeGamepad(Racecar* racecar) {
    Joystick* gamepad = getGamepad(racecar);

    if (gamepad) {
        gamepadList.removeAll(gamepad);
    }
}

void GamepadManager::run() {
    const bool debugGamepad = (QProcessEnvironment::systemEnvironment().value("ANKI_GAMEPAD_DEBUG") == "1");

    // Per-gamepad edge detection for the nitro toggle button
    // Key: joystick number, Value: last pressed state
    static QMap<int, bool> nitroPressed;

    while (true) {
        usleep(1000);

        if (!gamepadList.empty()) {
            foreach (Joystick* gamepad, gamepadList) {
                if (!gamepad || !gamepad->isFound() || !gamepad->getRacecar()) {
                    continue;
                }

                JoystickEvent event;

                if (gamepad->sample(&event)) {
                    // Ignore initial state events from Linux joystick API.
                    if (event.isInitialState()) {
                        continue;
                    }

                    if (debugGamepad) {
                        qDebug().noquote().nospace()
                            << "[GAMEPAD" << gamepad->getJoyStickNumber() << "] "
                            << "type=" << (int)event.type
                            << " num=" << (int)event.number
                            << " val=" << event.value;
                    }

                    if (event.isButton()) {
                        switch (event.number) {
                        case 0: { // Button A (commonly)
                            // Toggle nitro on button down edge.
                            const bool down = (event.value != 0);
                            const int joy = gamepad->getJoyStickNumber();
                            const bool wasDown = nitroPressed.value(joy, false);
                            nitroPressed[joy] = down;

                            if (down && !wasDown) {
                                // Reuse turboMode slot: true->enable, false->disable.
                                // We toggle based on current racecar turbo state.
                                const bool enable = !gamepad->getRacecar()->turboIsActive();
                                emit turboMode(gamepad->getRacecar(), enable);
                            }
                            break;
                        }
                        case 1: // Button B
                            if (!gamepad->buttonBInitialized()) {
                                gamepad->initializeButtonB();
                            }
                            else {
                                // Keep old behavior: hold-to-nitro on B
                                emit turboMode(gamepad->getRacecar(), (event.value)?true:false);
                            }
                            break;
                        case 2: // Button X
                            if (!gamepad->buttonXInitialized()) {
                                gamepad->initializeButtonX();
                            }
                            else {
                                emit doUturn(gamepad->getRacecar(), (event.value)?true:false);
                            }
                            break;
                        case 11: // Button Left
                            emit driveLeft(gamepad->getRacecar(), (event.value)?true:false);
                            break;
                        case 12: // Button Right
                            emit driveRight(gamepad->getRacecar(), (event.value)?true:false);
                            break;
                        }
                    }

                    if (event.isAxis()) {
                        switch (event.number) {
                        case 0: // Left joystick, x-axis
                            emit changeLane(gamepad->getRacecar(), mapAxisValue(event.value));
                            break;
                        // Triggers can be mapped differently depending on controller/driver.
                        // Observed (your logs): axis 4 acts like a trigger (wide range up to ~32767).
                        // Axis 3 on your device seems to jitter around small values (258/516) and
                        // should NOT be treated as throttle.
                        // Other common trigger axes on some controllers: 2, 5.
                        case 2:
                        case 4:
                        case 5: {
                            if (!gamepad->axisInitialized()) {
                                gamepad->initializeAxis();
                            }
                            else {
                                double v = mapTriggerValue(event.value);
                                // deadzone to avoid jitter causing unintended motion
                                if (v < 0.05) v = 0.0;
                                emit acceleratorChanged(gamepad->getRacecar(), v);
                            }
                            break;
                        }
                        }
                    }
                }
            }
        }
    }
}

// Convert joystick input to value between -1.0 and 1.0
double GamepadManager::mapAxisValue(short value) {
    return ((double)value / 32767.0);
}

// Convert trigger input to value between 0.0 and 1.0
double GamepadManager::mapTriggerValue(short value) {
    // Linux joystick trigger on some devices is 0..255, others are -32768..32767.
    if (value >= 0 && value <= 255) {
        return ((double)value / 255.0);
    }

    return ((double)(value + 32767) / 65534.0);
}
