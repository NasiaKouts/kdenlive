/***************************************************************************
 *   Copyright (C) 2017 by Jean-Baptiste Mardelle                                  *
 *   This file is part of Kdenlive. See www.kdenlive.org.                  *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) version 3 or any later version accepted by the       *
 *   membership of KDE e.V. (or its successor approved  by the membership  *
 *   of KDE e.V.), which shall act as a proxy defined in Section 14 of     *
 *   version 3 of the license.                                             *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 ***************************************************************************/

#include "timelinecontroller.h"
#include "../model/timelinefunctions.hpp"
#include "bin/bin.h"
#include "bin/model/markerlistmodel.hpp"
#include "bin/projectitemmodel.h"
#include "bin/projectclip.h"
#include "previewmanager.h"
#include "core.h"
#include "doc/kdenlivedoc.h"
#include "kdenlivesettings.h"
#include "project/projectmanager.h"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/model/trackmodel.hpp"
#include "timeline2/model/clipmodel.hpp"
#include "timeline2/model/compositionmodel.hpp"
#include "timeline2/model/groupsmodel.hpp"
#include "transitions/transitionsrepository.hpp"
#include "dialogs/spacerdialog.h"
#include "timelinewidget.h"
#include "utils/KoIconUtils.h"

#include <KActionCollection>
#include <QApplication>
#include <QQuickItem>

int TimelineController::m_duration = 0;

TimelineController::TimelineController(KActionCollection *actionCollection, QObject *parent)
    : QObject(parent)
    , m_root(nullptr)
    , m_actionCollection(actionCollection)
    , m_position(0)
    , m_seekPosition(-1)
    , m_scale(3.0)
    , m_usePreview(false)
    , m_timelinePreview(nullptr)
{
    m_disablePreview = pCore->currentDoc()->getAction(QStringLiteral("disable_preview"));
    connect(m_disablePreview, &QAction::triggered, this, &TimelineController::disablePreview);
    m_disablePreview->setEnabled(false);
}

void TimelineController::setModel(std::shared_ptr<TimelineItemModel> model)
{
    m_model = std::move(model);
    connect(m_model.get(), &TimelineItemModel::requestClearAssetView,
            [&](int id){pCore->clearAssetPanel(id);});
    connect(m_model.get(), &TimelineItemModel::requestMonitorRefresh,
            [&](){pCore->requestMonitorRefresh();});
}

std::shared_ptr<TimelineItemModel> TimelineController::getModel() const
{
    return m_model;
}

void TimelineController::setRoot(QQuickItem *root)
{
    m_root = root;
}

Mlt::Tractor *TimelineController::tractor()
{
    return m_model->tractor();
}

void TimelineController::addSelection(int newSelection)
{
    if (m_selection.selectedClips.contains(newSelection)) {
        return;
    }
    m_selection.selectedClips << newSelection;
    std::unordered_set<int> ids;
    ids.insert(m_selection.selectedClips.cbegin(), m_selection.selectedClips.cend());
    m_model->requestClipsGroup(ids, true, true);
    emit selectionChanged();

    if (!m_selection.selectedClips.isEmpty())
        emitSelectedFromSelection();
    else
        emit selected(nullptr);
}

int TimelineController::getCurrentItem()
{
    //TODO: if selection is empty, return topmost clip under timeline cursor
    if (m_selection.selectedClips.isEmpty()) {
        return -1;
    }
    //TODO: if selection contains more than 1 clip, return topmost clip under timeline cursor in selection
    return m_selection.selectedClips.constFirst();
}

double TimelineController::scaleFactor() const
{
    return m_scale;
}

const QString TimelineController::getTrackNameFromMltIndex(int trackPos)
{
    if (trackPos == -1) {
        return i18n("unknown");
    }
    if (trackPos == 0) {
        return i18n("Black");
    }
    return m_model->getTrackById(m_model->getTrackIndexFromPosition(trackPos -1))->getProperty(QStringLiteral("kdenlive:track_name")).toString();
}

const QString TimelineController::getTrackNameFromIndex(int trackIndex)
{
    return m_model->getTrackById(trackIndex)->getProperty(QStringLiteral("kdenlive:track_name")).toString();
}

QMap<int, QString> TimelineController::getTrackNames(bool videoOnly)
{
    QMap<int, QString> names;
    for (const auto &track : m_model->m_iteratorTable) {
        if (videoOnly && m_model->getTrackById(track.first)->getProperty(QStringLiteral("kdenlive:audio_track")).toInt() == 1) {
            continue;
        }
        names[m_model->getTrackMltIndex(track.first)] = m_model->getTrackById(track.first)->getProperty("kdenlive:track_name").toString();
    }
    return names;
}

void TimelineController::setScaleFactor(double scale)
{
    /*if (m_duration * scale < width() - 160) {
        // Don't allow scaling less than full project's width
        scale = (width() - 160.0) / m_duration;
    }*/
    m_scale = scale;
    emit scaleFactorChanged();
}

int TimelineController::duration() const
{
    return m_duration;
}

void TimelineController::checkDuration()
{
    int currentLength = m_model->duration();
    if (currentLength != m_duration) {
        m_duration = currentLength;
        emit durationChanged();
    }
}

void TimelineController::setSelection(const QList<int> &newSelection, int trackIndex, bool isMultitrack)
{
    if (newSelection != selection() || trackIndex != m_selection.selectedTrack || isMultitrack != m_selection.isMultitrackSelected) {
        qDebug() << "Changing selection to" << newSelection << " trackIndex" << trackIndex << "isMultitrack" << isMultitrack;
        m_selection.selectedClips = newSelection;
        m_selection.selectedTrack = trackIndex;
        m_selection.isMultitrackSelected = isMultitrack;
        std::unordered_set<int> ids;
        ids.insert(m_selection.selectedClips.cbegin(), m_selection.selectedClips.cend());
        m_model->requestClipsGroup(ids, true, true);
        emit selectionChanged();

        if (!m_selection.selectedClips.isEmpty())
            emitSelectedFromSelection();
        else
            emit selected(nullptr);
    }
}

void TimelineController::emitSelectedFromSelection()
{
    /*if (!m_model.trackList().count()) {
        if (m_model.tractor())
            selectMultitrack();
        else
            emit selected(0);
        return;
    }

    int trackIndex = currentTrack();
    int clipIndex = selection().isEmpty()? 0 : selection().first();
    Mlt::ClipInfo* info = getClipInfo(trackIndex, clipIndex);
    if (info && info->producer && info->producer->is_valid()) {
        delete m_updateCommand;
        m_updateCommand = new Timeline::UpdateCommand(*this, trackIndex, clipIndex, info->start);
        // We need to set these special properties so time-based filters
        // can get information about the cut while still applying filters
        // to the cut parent.
        info->producer->set(kFilterInProperty, info->frame_in);
        info->producer->set(kFilterOutProperty, info->frame_out);
        if (MLT.isImageProducer(info->producer))
            info->producer->set("out", info->cut->get_int("out"));
        info->producer->set(kMultitrackItemProperty, 1);
        m_ignoreNextPositionChange = true;
        emit selected(info->producer);
        delete info;
    }*/
}

QList<int> TimelineController::selection() const
{
    if (!m_root) return QList<int>();
    return m_selection.selectedClips;
}

void TimelineController::selectMultitrack()
{
    setSelection(QList<int>(), -1, true);
    QMetaObject::invokeMethod(m_root, "selectMultitrack");
    // emit selected(m_model.tractor());
}

bool TimelineController::snap()
{
    return KdenliveSettings::snaptopoints();
}

void TimelineController::snapChanged(bool snap)
{
    m_root->setProperty("snapping", snap ? 10 / std::sqrt(m_scale) : -1);
}

bool TimelineController::ripple()
{
    return false;
}

bool TimelineController::scrub()
{
    return false;
}

int TimelineController::insertClip(int tid, int position, const QString &data_str, bool logUndo)
{
    int id;
    if (tid == -1) {
        QVariant returnedValue;
        QMetaObject::invokeMethod(m_root, "currentTrackId", Q_RETURN_ARG(QVariant, returnedValue));
        tid = returnedValue.toInt();
    }
    if (position == -1) {
        position = m_position;
    }
    if (!m_model->requestClipInsertion(data_str, tid, position, id, logUndo)) {
        id = -1;
    }
    return id;
}

int TimelineController::insertComposition(int tid, int position, const QString &transitionId, bool logUndo)
{
    int id;
    if (!m_model->requestCompositionInsertion(transitionId, tid, position, 100, id, logUndo)) {
        id = -1;
    }
    return id;
}

void TimelineController::deleteSelectedClips()
{
    if (m_selection.selectedClips.isEmpty()) {
        return;
    }
    for (int cid : m_selection.selectedClips) {
        m_model->requestItemDeletion(cid);
    }
}

void TimelineController::triggerAction(const QString &name)
{
    QAction *action = m_actionCollection->action(name);
    if (action) {
        action->trigger();
    }
}

QString TimelineController::timecode(int frames)
{
    return KdenliveSettings::frametimecode() ? QString::number(frames) : m_model->tractor()->frames_to_time(frames, mlt_time_smpte_df);
}

bool TimelineController::showThumbnails() const
{
    return KdenliveSettings::videothumbnails();
}

bool TimelineController::showAudioThumbnails() const
{
    return KdenliveSettings::audiothumbnails();
}

bool TimelineController::showMarkers() const
{
    return KdenliveSettings::showmarkers();
}

bool TimelineController::audioThumbFormat() const
{
    return KdenliveSettings::displayallchannels();
}

bool TimelineController::showWaveforms() const
{
    return KdenliveSettings::audiothumbnails();
}

void TimelineController::addTrack(int tid)
{
    qDebug() << "Adding track: " << tid;
}

void TimelineController::deleteTrack(int tid)
{
    qDebug() << "Deleting track: " << tid;
}

void TimelineController::gotoNextSnap()
{
    setPosition(m_model->requestNextSnapPos(m_position));
}

void TimelineController::gotoPreviousSnap()
{
    setPosition(m_model->requestPreviousSnapPos(m_position));
}

void TimelineController::groupSelection()
{
    std::unordered_set<int> clips;
    for (int id : m_selection.selectedClips) {
        clips.insert(id);
    }
    m_model->requestClipsGroup(clips);
}

void TimelineController::unGroupSelection(int cid)
{
    if (cid == -1 && m_selection.selectedClips.isEmpty()) {
        return;
    }
    if (cid == -1) {
        for (int id : m_selection.selectedClips) {
            if (m_model->m_groups->isInGroup(id) && !m_model->isInSelection(id)) {
                cid = id;
                break;
            }
        }
    }
    if (cid > -1) {
        m_model->requestClipUngroup(cid);
        m_selection.selectedClips.clear();
        emit selectionChanged();
    }
}

void TimelineController::setInPoint()
{
    int cursorPos = m_seekPosition;
    if (cursorPos < 0) {
        cursorPos = m_position;
    }
    if (!m_selection.selectedClips.isEmpty()) {
        for (int id : m_selection.selectedClips) {
            m_model->requestItemResizeToPos(id, cursorPos, false);
        }
    }
}

void TimelineController::setOutPoint()
{
    int cursorPos = m_seekPosition;
    if (cursorPos < 0) {
        cursorPos = m_position;
    }
    if (!m_selection.selectedClips.isEmpty()) {
        for (int id : m_selection.selectedClips) {
            m_model->requestItemResizeToPos(id, cursorPos, true);
        }
    }
}

void TimelineController::editMarker(const QString &cid, int frame)
{
    std::shared_ptr<ProjectClip> clip = pCore->bin()->getBinClip(cid);
    GenTime pos(frame, pCore->getCurrentFps());
    clip->getMarkerModel()->editMarkerGui(pos, qApp->activeWindow(), false, clip.get());
}

void TimelineController::editGuide(int frame)
{
    if (frame == -1) {
        frame = m_position;
    }
    auto guideModel = pCore->projectManager()->current()->getGuideModel();
    GenTime pos(frame, pCore->getCurrentFps());
    guideModel->editMarkerGui(pos, qApp->activeWindow(), false);
}

void TimelineController::switchGuide(int frame, bool deleteOnly)
{
    bool markerFound = false;
    if (frame == -1) {
        frame = m_position;
    }
    CommentedTime marker = pCore->projectManager()->current()->getGuideModel()->getMarker(GenTime(frame, pCore->getCurrentFps()), &markerFound);
    if (!markerFound) {
        if (deleteOnly) {
            pCore->displayMessage(i18n("No guide found at current position"), InformationMessage, 500);
            return;
        }
        GenTime pos(frame, pCore->getCurrentFps());
        pCore->projectManager()->current()->getGuideModel()->addMarker(pos, i18n("guide"));
    } else {
        pCore->projectManager()->current()->getGuideModel()->removeMarker(marker.time());
    }
}

void TimelineController::addAsset(const QVariantMap data)
{
    QString effect = data.value(QStringLiteral("kdenlive/effect")).toString();
    if (!m_selection.selectedClips.isEmpty()) {
        for (int id : m_selection.selectedClips) {
            m_model->addClipEffect(id, effect);
        }
    } else {
        pCore->displayMessage(i18n("Select a clip to apply an effect"), InformationMessage, 500);
    }
}

void TimelineController::requestRefresh()
{
    pCore->requestMonitorRefresh();
}

void TimelineController::showAsset(int id)
{
    if (m_model->isComposition(id)) {
        emit showTransitionModel(id, m_model->getCompositionParameterModel(id));
    } else if (m_model->isClip(id)) {
        QModelIndex clipIx = m_model->makeClipIndexFromID(id);
        QString clipName = m_model->data(clipIx, Qt::DisplayRole).toString();
        emit showClipEffectStack(clipName, m_model->getClipEffectStackModel(id),
                                 QPair<int, int>(m_model->getClipPosition(id), m_model->getClipPosition(id) + m_model->getClipPlaytime(id)));
    }
}

void TimelineController::setPosition(int position)
{
    setSeekPosition(position);
    emit seeked(position);
}

void TimelineController::setSeekPosition(int position)
{
    m_seekPosition = position;
    emit seekPositionChanged();
}

void TimelineController::onSeeked(int position)
{
    m_position = position;
    emit positionChanged();
    if (m_seekPosition > -1 && position == m_seekPosition) {
        m_seekPosition = -1;
        emit seekPositionChanged();
    }
}

void TimelineController::setZone(const QPoint &zone)
{
    m_zone = zone;
    emit zoneChanged();
}

void TimelineController::setZoneIn(int inPoint)
{
    m_zone.setX(inPoint);
    emit zoneMoved(m_zone);
}

void TimelineController::setZoneOut(int outPoint)
{
    m_zone.setY(outPoint);
    emit zoneMoved(m_zone);
}

void TimelineController::selectItems(QVariantList arg, int startFrame, int endFrame)
{
    m_selection.selectedClips.clear();
    std::unordered_set<int>clipsToSelect;
    for (int i = 0; i < arg.count(); i++) {
        std::unordered_set<int> trackClips = m_model->getTrackById(arg.at(i).toInt())->getClipsAfterPosition(startFrame, endFrame);
        clipsToSelect.insert(trackClips.begin(), trackClips.end());
    }
    for (int x: clipsToSelect) {
        m_selection.selectedClips << x;
    }
    m_model->requestClipsGroup(clipsToSelect, true, true);
    emit selectionChanged();
}

void TimelineController::cutClipUnderCursor(int position, int track)
{
    if (position == -1) {
        position = m_position;
    }
    bool foundClip = false;
    for (int cid : m_selection.selectedClips) {
        if (TimelineFunctions::requestClipCut(m_model, cid, position)) {
            foundClip = true;
        }
    }
    if (!foundClip) {
        if (track == -1) {
            QVariant returnedValue;
            QMetaObject::invokeMethod(m_root, "currentTrackId", Q_RETURN_ARG(QVariant, returnedValue));
            track = returnedValue.toInt();
        }
        if (track >= 0) {
            int cid = m_model->getClipByPosition(track, position);
            if (cid >= 0) {
                TimelineFunctions::requestClipCut(m_model, cid, position);
                foundClip = true;
            }
        }
    }
    if (!foundClip) {
        // TODO: display warning, no clip found
    }
}

int TimelineController::requestSpacerStartOperation(int trackId, int position)
{
    return TimelineFunctions::requestSpacerStartOperation(m_model, trackId, position);
}

bool TimelineController::requestSpacerEndOperation(int clipId, int startPosition, int endPosition)
{
    return TimelineFunctions::requestSpacerEndOperation(m_model, clipId, startPosition, endPosition);
}

void TimelineController::seekCurrentClip(bool seekToEnd)
{
    bool foundClip = false;
    for (int cid : m_selection.selectedClips) {
        int start = m_model->getItemPosition(cid);
        if (seekToEnd) {
            start += m_model->getItemPlaytime(cid);
        }
        setPosition(start);
        foundClip = true;
        break;
    }
}

void TimelineController::seekToClip(int cid, bool seekToEnd)
{
    int start = m_model->getItemPosition(cid);
    if (seekToEnd) {
        start += m_model->getItemPlaytime(cid);
    }
    setPosition(start);
}

void TimelineController::seekToMouse()
{
    QVariant returnedValue;
    QMetaObject::invokeMethod(m_root, "getMousePos", Q_RETURN_ARG(QVariant, returnedValue));
    int mousePos = returnedValue.toInt();
    setPosition(mousePos);
}

int TimelineController::getMousePos()
{
    QVariant returnedValue;
    QMetaObject::invokeMethod(m_root, "getMousePos", Q_RETURN_ARG(QVariant, returnedValue));
    return returnedValue.toInt();
}

void TimelineController::refreshItem(int id)
{
    int in = m_model->getItemPosition(id);
    if (in > m_position) {
        return;
    }
    if (m_position <= in + m_model->getItemPlaytime(id)) {
        pCore->requestMonitorRefresh();
    }
}

QPoint TimelineController::getTracksCount() const
{
    int audioTracks = 0;
    int videoTracks = 0;
    QVariant returnedValue;
    QMetaObject::invokeMethod(m_root, "getTracksCount", Q_RETURN_ARG(QVariant, returnedValue));
    QVariantList tracks = returnedValue.toList();
    QPoint p(tracks.at(0).toInt(), tracks.at(1).toInt());
    return p;
}

QStringList TimelineController::extractCompositionLumas() const
{
    return m_model->extractCompositionLumas();
}

void TimelineController::addEffectToCurrentClip(const QStringList &effectData)
{
    QList <int> activeClips;
    for (int track = m_model->getTracksCount() - 1; track > 0; track--) {
        int trackIx = m_model->getTrackIndexFromPosition(track);
        int cid = m_model->getClipByPosition(trackIx, m_position);
        if (cid > -1) {
            activeClips << cid;
        }
    }
    if (!activeClips.isEmpty()) {
        if (effectData.count() == 4) {
            QString effectString = effectData.at(1) + QStringLiteral("-") + effectData.at(2) + QStringLiteral("-") + effectData.at(3);
            m_model->copyClipEffect(activeClips.first(), effectString);
        } else {
            m_model->addClipEffect(activeClips.first(), effectData.constFirst());
        }
    }
}

void TimelineController::adjustFade(int cid, const QString &effectId, int duration)
{
    if (duration <= 0) {
        // remove fade
        m_model->removeFade(cid, effectId == QLatin1String("fadein"));
    } else {
        m_model->adjustEffectLength(cid, effectId, duration);
        QModelIndex ix = m_model->makeClipIndexFromID(cid);
        QVector <int> roles;
        roles << TimelineModel::FadeInRole;
        m_model->dataChanged(ix, ix, roles);
    }
}

int TimelineController::getCompositionATrack(int cid) const
{
    return m_model->getCompositionPtr(cid)->getATrack();
}

void TimelineController::setCompositionATrack(int cid, int aTrack)
{
    QScopedPointer<Mlt::Field> field(m_model->m_tractor->field());
    field->lock();
    m_model->getCompositionPtr(cid)->setATrack(aTrack);
    field->unlock();
    refreshItem(cid);
    QModelIndex modelIndex = m_model->makeCompositionIndexFromID(cid);
    QVector <int> roles;
    roles << TimelineModel::ItemATrack;
    m_model->dataChanged(modelIndex, modelIndex, roles);
}

const QString TimelineController::getClipBinId(int clipId) const
{
    return m_model->getClipBinId(clipId);
}

void TimelineController::focusItem(int itemId)
{
    int start = m_model->getItemPosition(itemId);
    setPosition(start);
}

int TimelineController::headerWidth() const
{
    return qMax(10, KdenliveSettings::headerwidth());
}

void TimelineController::setHeaderWidth(int width)
{
    KdenliveSettings::setHeaderwidth(width);
}

bool TimelineController::createSplitOverlay(Mlt::Filter *filter)
{
    if (m_timelinePreview && m_timelinePreview->hasOverlayTrack()) {
        return true;
    }
    int clipId = getCurrentItem();
    if (clipId == -1) {
        pCore->displayMessage(i18n("Select a clip to compare effect"), InformationMessage, 500);
        return false;
    }
    std::shared_ptr<ClipModel> clip = m_model->getClipPtr(clipId);
    const QString binId = clip->binId();

    // Get clean bin copy of the clip
    std::shared_ptr< ProjectClip > binClip = pCore->projectItemModel()->getClipByBinID(binId);
    std::shared_ptr<Mlt::Producer> binProd(binClip->masterProducer()->cut(clip->getIn(), clip->getOut()));

    // Get copy of timeline producer
    Mlt::Producer *clipProducer = new Mlt::Producer(*clip);

    // Built tractor and compositing
    Mlt::Tractor trac(*m_model->m_tractor->profile());
    Mlt::Playlist play(*m_model->m_tractor->profile());
    Mlt::Playlist play2(*m_model->m_tractor->profile());
    play.append(*clipProducer);
    play2.append(*binProd);
    trac.set_track(play, 0);
    trac.set_track(play2, 1);
    play2.attach(*filter);
    QString splitTransition = TransitionsRepository::get()->getCompositingTransition();
    Mlt::Transition t(*m_model->m_tractor->profile(), splitTransition.toUtf8().constData());
    t.set("always_active", 1);
    trac.plant_transition(t, 0, 1);
    int startPos = m_model->getClipPosition(clipId);

    // plug in overlay playlist
    Mlt::Playlist *overlay = new Mlt::Playlist(*m_model->m_tractor->profile());
    overlay->insert_blank(0, startPos);
    Mlt::Producer split(trac.get_producer());
    overlay->insert_at(startPos, &split, 1);

    // insert in tractor
    if (!m_timelinePreview) {
        initializePreview();
    }
    m_timelinePreview->setOverlayTrack(overlay);
    m_model->m_overlayTrackCount = m_timelinePreview->addedTracks();
    return true;

}

void TimelineController::removeSplitOverlay()
{
    if (m_timelinePreview && !m_timelinePreview->hasOverlayTrack()) {
        return;
    }
    // disconnect
    m_timelinePreview->removeOverlayTrack();
    m_model->m_overlayTrackCount = m_timelinePreview->addedTracks();
}

void TimelineController::addPreviewRange(bool add)
{
    if (m_timelinePreview && !m_zone.isNull()) {
        m_timelinePreview->addPreviewRange(m_zone, add);
    }
}

void TimelineController::clearPreviewRange()
{
    if (m_timelinePreview) {
        m_timelinePreview->clearPreviewRange();
    }
}

void TimelineController::startPreviewRender()
{
    // Timeline preview stuff
    if (!m_timelinePreview) {
        initializePreview();
    } else if (m_disablePreview->isChecked()) {
        m_disablePreview->setChecked(false);
        disablePreview(false);
    }
    if (m_timelinePreview) {
        if (!m_usePreview) {
            m_timelinePreview->buildPreviewTrack();
            qDebug()<<"// STARTING PREVIEW TRACK";
            m_usePreview = true;
            m_model->m_overlayTrackCount = m_timelinePreview->addedTracks();
        }
        m_timelinePreview->startPreviewRender();
    }
}

void TimelineController::stopPreviewRender()
{
    if (m_timelinePreview) {
        m_timelinePreview->abortRendering();
    }
}

void TimelineController::initializePreview()
{
    if (m_timelinePreview) {
        // Update parameters
        if (!m_timelinePreview->loadParams()) {
            if (m_usePreview) {
                // Disconnect preview track
                m_timelinePreview->disconnectTrack();
                m_usePreview = false;
            }
            delete m_timelinePreview;
            m_timelinePreview = nullptr;
        }
    } else {
        m_timelinePreview = new PreviewManager(this, m_model->m_tractor.get());
        if (!m_timelinePreview->initialize()) {
            // TODO warn user
            delete m_timelinePreview;
            m_timelinePreview = nullptr;
        } else {
        }
    }
    QAction *previewRender = pCore->currentDoc()->getAction(QStringLiteral("prerender_timeline_zone"));
    if (previewRender) {
        previewRender->setEnabled(m_timelinePreview != nullptr);
    }
    m_disablePreview->setEnabled(m_timelinePreview != nullptr);
    m_disablePreview->blockSignals(true);
    m_disablePreview->setChecked(false);
    m_disablePreview->blockSignals(false);
}

void TimelineController::disablePreview(bool disable)
{
    if (disable) {
        m_timelinePreview->deletePreviewTrack();
        m_usePreview = false;
        m_model->m_overlayTrackCount = m_timelinePreview->addedTracks();
    } else {
        if (!m_usePreview) {
            if (!m_timelinePreview->buildPreviewTrack()) {
                // preview track already exists, reconnect
                m_model->m_tractor->lock();
                m_timelinePreview->reconnectTrack();
                m_model->m_tractor->unlock();
            }
            m_timelinePreview->loadChunks(QVariantList(), QVariantList(), QDateTime());
            m_usePreview = true;
        }
    }
    m_model->m_overlayTrackCount = m_timelinePreview->addedTracks();
}

QVariantList TimelineController::dirtyChunks() const
{
    return m_timelinePreview ? m_timelinePreview->m_dirtyChunks : QVariantList();
}

QVariantList TimelineController::renderedChunks() const
{
    return m_timelinePreview ? m_timelinePreview->m_renderedChunks : QVariantList();
}

int TimelineController::workingPreview() const
{
    return m_timelinePreview ? m_timelinePreview->workingPreview : -1;
}

void TimelineController::loadPreview(QString chunks, QString dirty, const QDateTime &documentDate, int enable)
{
    if (!m_timelinePreview) {
        initializePreview();
    }
    QVariantList renderedChunks;
    QVariantList dirtyChunks;
    QStringList chunksList = chunks.split(QLatin1Char(','), QString::SkipEmptyParts);
    QStringList dirtyList = dirty.split(QLatin1Char(','), QString::SkipEmptyParts);
    for (const QString &frame : chunksList) {
        renderedChunks << frame.toInt();
    }
    for (const QString &frame : dirtyList) {
        dirtyChunks << frame.toInt();
    }
    m_disablePreview->blockSignals(true);
    m_disablePreview->setChecked(enable);
    m_disablePreview->blockSignals(false);
    if (!enable) {
        m_timelinePreview->buildPreviewTrack();
    }
    m_timelinePreview->loadChunks(renderedChunks, dirtyChunks, documentDate);
}

QMap<QString, QString> TimelineController::documentProperties()
{
    QMap<QString, QString> props = pCore->currentDoc()->documentProperties();
    //TODO
    //props.insert(QStringLiteral("audiotargettrack"), QString::number(audioTarget));
    //props.insert(QStringLiteral("videotargettrack"), QString::number(videoTarget));
    if (m_timelinePreview) {
        QPair<QStringList, QStringList> chunks = m_timelinePreview->previewChunks();
        props.insert(QStringLiteral("previewchunks"), chunks.first.join(QLatin1Char(',')));
        props.insert(QStringLiteral("dirtypreviewchunks"), chunks.second.join(QLatin1Char(',')));
    }
    props.insert(QStringLiteral("disablepreview"), QString::number((int)m_disablePreview->isChecked()));
    return props;
}

void TimelineController::insertSpace(int trackId, int frame)
{
    if (frame == -1) {
        frame = m_position;
    }
    if (trackId == -1) {
        QVariant returnedValue;
        QMetaObject::invokeMethod(m_root, "currentTrackId", Q_RETURN_ARG(QVariant, returnedValue));
        trackId = returnedValue.toInt();
    }
    QPointer<SpacerDialog> d = new SpacerDialog(GenTime(65, pCore->getCurrentFps()), pCore->currentDoc()->timecode(), qApp->activeWindow());
    if (d->exec() != QDialog::Accepted) {
        delete d;
        return;
    }
    int cid = requestSpacerStartOperation(d->affectAllTracks() ? -1 : trackId, frame);
    int spaceDuration = d->selectedDuration().frames(pCore->getCurrentFps());
    delete d;
    if (cid == -1) {
        pCore->displayMessage(i18n("No clips found to insert space"), InformationMessage, 500);
        return;
    }
    int start = m_model->getItemPosition(cid);
    requestSpacerEndOperation(cid, start, start + spaceDuration);
}

void TimelineController::removeSpace(int trackId, int frame, bool affectAllTracks)
{
    if (frame == -1) {
        frame = m_position;
    }
    if (trackId == -1) {
        QVariant returnedValue;
        QMetaObject::invokeMethod(m_root, "currentTrackId", Q_RETURN_ARG(QVariant, returnedValue));
        trackId = returnedValue.toInt();
    }
    // find blank duration
    int spaceDuration = m_model->getTrackById(trackId)->getBlankSizeAtPos(frame);
    int cid = requestSpacerStartOperation(affectAllTracks ? -1 : trackId, frame);
    if (cid == -1) {
        pCore->displayMessage(i18n("No clips found to insert space"), InformationMessage, 500);
        return;
    }
    int start = m_model->getItemPosition(cid);
    requestSpacerEndOperation(cid, start, start - spaceDuration);
}
