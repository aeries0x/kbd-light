// SPDX-License-Identifier: MIT
#pragma once

#include <KQuickManagedConfigModule>
#include <QFileSystemWatcher>
#include <QHash>
#include <QTimer>
#include <QVariantMap>

#include "displaylightsettings.h" // generated from the .kcfgc; a complete type for the property

/*
 * System Settings page for aurora-display-light. The settings themselves are
 * plain KConfig entries (aurora-display-lightsettings.kcfg) that the service
 * re-reads when the file changes; this class adds what QML can't do alone:
 * the built-in profiles, the adjustments the service has learned, its live
 * status, and the curve for drawing.
 */
class DisplayLightKcm : public KQuickManagedConfigModule
{
    Q_OBJECT
    Q_PROPERTY(DisplayLightSettings *settings READ settings CONSTANT)
    Q_PROPERTY(QVariantMap status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool running READ running NOTIFY statusChanged)
    Q_PROPERTY(double maxNits READ maxNits NOTIFY statusChanged)
    Q_PROPERTY(QVariantList pointLux READ pointLux CONSTANT)
    Q_PROPERTY(bool hasLearned READ hasLearned NOTIFY learnedChanged)

public:
    DisplayLightKcm(QObject *parent, const KPluginMetaData &data);

    DisplayLightSettings *settings() const;
    QVariantMap status() const;
    bool running() const;
    double maxNits() const;
    QVariantList pointLux() const;
    bool hasLearned() const;

    /* The nits of a profile at pointLux, as the page is set now ("custom" reads the unsaved values). */
    Q_INVOKABLE QVariantList profileNits(const QString &profile) const;
    /* What the service has learned for a profile, in the perceptual units it uses, one per point. */
    Q_INVOKABLE QVariantList learnedOffsets(const QString &profile) const;
    /* The curve a profile gives at lux, with or without what has been learned. */
    Q_INVOKABLE double curveNits(const QString &profile, double lux, bool withLearned) const;
    /* Start a custom curve from a profile as it is now, learned adjustments included. */
    Q_INVOKABLE void copyToCustom(const QString &profile);
    /* Forget everything learned, for every profile. */
    Q_INVOKABLE void forgetLearned();

Q_SIGNALS:
    void statusChanged();
    void learnedChanged();

private:
    void readStatus();
    void readLearned();
    QString learnedPath() const;
    QString statusPath() const;

    DisplayLightSettings *m_settings;
    QVariantMap m_status;
    QHash<QString, QVariantList> m_learned; // profile -> offsets, as the service saved them
    QTimer m_statusTimer;
    QFileSystemWatcher m_learnedWatcher;
};
