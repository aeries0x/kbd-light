// SPDX-License-Identifier: MIT
#include "displaylightkcm.h"

#include <KPluginFactory>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QQmlEngine>
#include <QStandardPaths>
#include <QTextStream>

#include <algorithm>
#include <array>
#include <cmath>

#include "profiles.h"

K_PLUGIN_CLASS_WITH_JSON(DisplayLightKcm, "kcm_aurora_display_light.json")

namespace
{
constexpr int Points = 5;
constexpr std::array<double, Points> PointLux = {DL_POINT_LUX};
constexpr double MinNits = 1; // the panel's lowest level above off, as the service uses
constexpr double DefaultMaxNits = 500;

struct Profile {
    const char *name;
    std::array<double, Points> nits;
};

#define DL_KCM_PROFILE(name, ...) Profile{#name, {__VA_ARGS__}},
constexpr Profile Profiles[] = {DL_PROFILES(DL_KCM_PROFILE)};

double perceptual(double nits, double max)
{
    nits = std::clamp(nits, MinNits, max);
    return std::log(nits / MinNits) / std::log(max / MinNits);
}

double fromPerceptual(double p, double max)
{
    return MinNits * std::pow(max / MinNits, std::clamp(p, 0.0, 1.0));
}

double luxX(double lux)
{
    return std::log10(1 + std::clamp(lux, PointLux.front(), PointLux.back()));
}
}

DisplayLightKcm::DisplayLightKcm(QObject *parent, const KPluginMetaData &data)
    : KQuickManagedConfigModule(parent, data)
    , m_settings(new DisplayLightSettings(this))
{
    qmlRegisterAnonymousType<DisplayLightSettings>("org.aurora.displaylight", 1);
    setButtons(Apply | Default);

    readStatus();
    readLearned();
    connect(&m_statusTimer, &QTimer::timeout, this, &DisplayLightKcm::readStatus);
    m_statusTimer.start(1000);

    // The service writes the file by renaming a new one over it: watch the directory.
    const QString dir = QFileInfo(learnedPath()).absolutePath();
    QDir().mkpath(dir);
    m_learnedWatcher.addPath(dir);
    connect(&m_learnedWatcher, &QFileSystemWatcher::directoryChanged, this, &DisplayLightKcm::readLearned);
}

DisplayLightSettings *DisplayLightKcm::settings() const
{
    return m_settings;
}

QString DisplayLightKcm::statusPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/aurora-display-light/status");
}

QString DisplayLightKcm::learnedPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) + QStringLiteral("/aurora-display-light/learned");
}

void DisplayLightKcm::readStatus()
{
    QVariantMap status;
    QFile file(statusPath());

    // A status older than a few seconds is from a service that is no longer running.
    if (file.open(QIODevice::ReadOnly) && QFileInfo(file).lastModified().secsTo(QDateTime::currentDateTime()) < 5) {
        QTextStream in(&file);
        while (!in.atEnd()) {
            const QString line = in.readLine();
            const qsizetype eq = line.indexOf(QLatin1Char('='));
            if (eq <= 0) {
                continue;
            }
            const QString key = line.left(eq);
            const QString value = line.mid(eq + 1);
            bool number = false;
            const double d = value.toDouble(&number);
            status.insert(key, number && std::isfinite(d) ? QVariant(d) : QVariant(value));
        }
    }
    if (status != m_status) {
        m_status = status;
        Q_EMIT statusChanged();
    }
}

QVariantMap DisplayLightKcm::status() const
{
    return m_status;
}

bool DisplayLightKcm::running() const
{
    const QString state = m_status.value(QStringLiteral("state")).toString();
    return !state.isEmpty() && state != QLatin1String("stopped");
}

double DisplayLightKcm::maxNits() const
{
    const double max = m_status.value(QStringLiteral("max")).toDouble();
    return max > MinNits ? max : DefaultMaxNits;
}

QVariantList DisplayLightKcm::pointLux() const
{
    QVariantList lux;
    for (double l : PointLux) {
        lux.append(l);
    }
    return lux;
}

QVariantList DisplayLightKcm::profileNits(const QString &profile) const
{
    QVariantList nits;
    for (const Profile &p : Profiles) {
        if (profile == QLatin1String(p.name)) {
            for (double n : p.nits) {
                nits.append(std::min(n, maxNits()));
            }
            return nits;
        }
    }
    const double last[Points] = {m_settings->darkNits(), m_settings->dimNits(), m_settings->indoorNits(), m_settings->brightNits(), m_settings->daylightNits()};
    double floor = MinNits;
    for (double n : last) {
        // as the service reads it: never falling as the light rises, within the panel
        floor = std::max(floor, std::min(n, maxNits()));
        nits.append(floor);
    }
    return nits;
}

void DisplayLightKcm::readLearned()
{
    QHash<QString, QVariantList> learned;
    QFile file(learnedPath());
    if (file.open(QIODevice::ReadOnly)) {
        QTextStream in(&file);
        while (!in.atEnd()) {
            const QStringList fields = in.readLine().split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (fields.size() != Points + 1) {
                continue;
            }
            QVariantList offsets;
            for (int i = 0; i < Points; i++) {
                offsets.append(std::clamp(fields[i + 1].toDouble(), -1.0, 1.0));
            }
            learned.insert(fields.first().toLower(), offsets);
        }
    }
    if (learned != m_learned) {
        m_learned = learned;
        Q_EMIT learnedChanged();
    }
}

QVariantList DisplayLightKcm::learnedOffsets(const QString &profile) const
{
    QVariantList none;
    for (int i = 0; i < Points; i++) {
        none.append(0.0);
    }
    return m_learned.value(profile.toLower(), none);
}

bool DisplayLightKcm::hasLearned() const
{
    return !m_learned.isEmpty();
}

double DisplayLightKcm::curveNits(const QString &profile, double lux, bool withLearned) const
{
    const QVariantList nits = profileNits(profile);
    const QVariantList offsets = withLearned ? learnedOffsets(profile) : QVariantList();
    const double max = maxNits();
    const double x = luxX(lux);
    int j = 0;
    while (j < Points - 2 && x > luxX(PointLux[j + 1])) {
        j++;
    }
    const double f = std::clamp((x - luxX(PointLux[j])) / (luxX(PointLux[j + 1]) - luxX(PointLux[j])), 0.0, 1.0);
    auto point = [&](int i) {
        const double offset = offsets.isEmpty() ? 0 : offsets[i].toDouble();
        return std::clamp(perceptual(nits[i].toDouble(), max) + offset, 0.0, 1.0);
    };
    return fromPerceptual(point(j) * (1 - f) + point(j + 1) * f, max);
}

void DisplayLightKcm::copyToCustom(const QString &profile)
{
    std::array<double, Points> nits;
    for (int i = 0; i < Points; i++) {
        nits[i] = std::round(curveNits(profile, PointLux[i], true));
    }
    m_settings->setDarkNits(nits[0]);
    m_settings->setDimNits(nits[1]);
    m_settings->setIndoorNits(nits[2]);
    m_settings->setBrightNits(nits[3]);
    m_settings->setDaylightNits(nits[4]);
}

void DisplayLightKcm::forgetLearned()
{
    QFile::remove(learnedPath());
    readLearned();
}

#include "displaylightkcm.moc"
