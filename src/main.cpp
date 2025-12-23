/*
 * Copyright (c) 9.11.2016 com2m GmbH.
 * All rights reserved.
 */

#include <QCoreApplication>
#include <QStringList>

#include "headers/drivemode.h"
#include <signal.h>
#include <iostream>

DriveMode* driveMode;

void disconnectCars(int sig) {
    (void)sig;
    driveMode->quit();
    exit(0);
}

static int parseCarsArg(const QStringList& args) {
    int cars = 4;
    for (int i = 1; i < args.size(); ++i) {
        const QString a = args.at(i);
        if (a == "--help" || a == "-h") {
            std::cout << "Usage: ankioverdrive [--cars N]\n"
                         "  --cars N   number of active cars to use (1..7), default 4\n";
            std::exit(0);
        }
        if (a == "--cars") {
            if (i + 1 >= args.size()) {
                std::cerr << "--cars requires a value (1..7)\n";
                std::exit(2);
            }
            bool ok = false;
            cars = args.at(i + 1).toInt(&ok);
            if (!ok) {
                std::cerr << "Invalid --cars value\n";
                std::exit(2);
            }
            i++;
        }
    }
    return cars;
}

int main(int argc, char *argv[]) {
    QCoreApplication a(argc, argv);

#ifndef DISABLE_MQTT
    qRegisterMetaType<MqttMessage>("MqttMessage");
#endif

    const QStringList args = QCoreApplication::arguments();
    const int cars = parseCarsArg(args);

    driveMode = new DriveMode(cars);

    signal(SIGINT, disconnectCars);

    return a.exec();
}
