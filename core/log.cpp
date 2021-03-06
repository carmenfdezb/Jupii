/* Copyright (C) 2019 Michal Kosciesza <michal@mkiol.net>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "log.h"

#include <QStandardPaths>
#include <QDir>
#include <QByteArray>
#include <cstdlib>
#include <libavutil/log.h>

FILE * logFile = nullptr;
FILE * ffmpegLogFile = nullptr;

void qtLog(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    if (!logFile) {
        QDir home(QStandardPaths::writableLocation(QStandardPaths::HomeLocation));
        auto file = home.filePath(LOG_FILE).toLatin1();
        logFile = fopen(file.data(), "w");
    }

    QByteArray localMsg = msg.toLocal8Bit();
    //const char *file = context.file ? context.file : "";
    const char *function = context.function ? context.function : "";

    char t = '-';
    switch (type) {
    case QtDebugMsg:
        t = 'D';
        break;
    case QtInfoMsg:
        t = 'I';
        break;
    case QtWarningMsg:
        t = 'W';
        break;
    case QtCriticalMsg:
        t = 'C';
        break;
    case QtFatalMsg:
        t = 'F';
        break;
    }

    fprintf(logFile, "[%c] %s:%u - %s\n", t, function, context.line, localMsg.constData());
    fflush(logFile);
}

void ffmpegLog(void *ptr, int level, const char *fmt, va_list vargs)
{
    if (!ffmpegLogFile) {
        QDir home(QStandardPaths::writableLocation(QStandardPaths::HomeLocation));
        auto file = home.filePath(FFMPEG_LOG_FILE).toLatin1();
        ffmpegLogFile = fopen(file.data(), "w");
    }

    fprintf(ffmpegLogFile, "FFMPEG [%s] - ",
            level == AV_LOG_TRACE ? "T" :
            level == AV_LOG_DEBUG ? "D" :
            level == AV_LOG_VERBOSE ? "V" :
            level == AV_LOG_INFO ? "I" :
            level == AV_LOG_WARNING ? "W" :
            level == AV_LOG_ERROR ? "E" :
            level == AV_LOG_FATAL ? "F" :
            level == AV_LOG_PANIC ? "P" : "-");
    vfprintf(ffmpegLogFile, fmt, vargs);
    fflush(ffmpegLogFile);
}
