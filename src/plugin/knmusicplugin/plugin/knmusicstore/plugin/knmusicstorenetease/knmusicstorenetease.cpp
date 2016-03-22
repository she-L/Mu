/*
 * Copyright (C) Kreogist Dev Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */
#include <QJsonArray>
#include <QJsonDocument>
#include <QDateTime>
#include <QNetworkCookie>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QImage>
#include <QScopedPointer>
#include <QCryptographicHash>

#include "knrestapibase.h"

#include "knmusicutil.h"

#include "../../sdk/knmusicstorealbumlistmodel.h"
#include "../../sdk/knmusicstorealbummodel.h"

#include "knmusicstorenetease.h"

#include <QDebug>

#define ListMaximumSize 18

KNMusicStoreNetease::KNMusicStoreNetease(QObject *parent) :
    KNMusicStoreBackend(parent),
    m_magicData(QByteArray("3go8&$8*3*3h0k(2)2")),
    m_magicDataLength(m_magicData.size()),
    m_newAlbumModel(new KNMusicStoreAlbumListModel),
    m_hotSongModel(new KNMusicStoreAlbumListModel),
    m_albumDetail(new KNMusicStoreAlbumModel)
{
    //Initial the list model.
    for(int i=0; i<NeteaseListCount; ++i)
    {
        m_listModel[i]=new KNMusicStoreAlbumListModel;
    }
}

KNMusicStoreNetease::~KNMusicStoreNetease()
{
    //Remove the new album model and hot song model.
    delete m_newAlbumModel;
    m_newAlbumModel=nullptr;
    delete m_hotSongModel;
    m_hotSongModel=nullptr;
    //Remove album detail model.
    delete m_albumDetail;
    m_albumDetail=nullptr;
    //Remove the list model.
    for(int i=0; i<NeteaseListCount; ++i)
    {
        //Reset the pointer.
        delete m_listModel[i];
        m_listModel[i]=nullptr;
    }

    //Cancel all the list threads.
    //Remove the list model.
    for(int i=0; i<NeteaseWorkThreadCount; ++i)
    {
        //Cancel and wait for thread stop.
        m_listThreads[i].cancel();
    }
    //Wait for thread quit.
    for(int i=0; i<NeteaseWorkThreadCount; ++i)
    {
        //Cancel and wait for thread stop.
        m_listThreads[i].waitForFinished();
    }
}

KNMusicStoreAlbumListModel *KNMusicStoreNetease::newAlbumModel()
{
    //Give back the model pointer.
    return m_newAlbumModel;
}

KNMusicStoreAlbumListModel *KNMusicStoreNetease::hotSongModel()
{
    //Give back the model pointer.
    return m_hotSongModel;
}

int KNMusicStoreNetease::listCount()
{
    //Give back the list count.
    return NeteaseListCount;
}

QString KNMusicStoreNetease::listName(int listIndex)
{
    //Check the list index.
    switch (listIndex)
    {
    case BillboardList:
        return tr("Billboard Week List");
    case iTunesList:
        return tr("iTunes Hot List");
    case OriconList:
        return tr("Oricon Week List");
    default:
        return QString();
    }
}

KNMusicStoreAlbumListModel *KNMusicStoreNetease::listModel(int listIndex)
{
    Q_ASSERT(listIndex < NeteaseListCount && listIndex > -1);
    //Give back the pointer.
    return m_listModel[listIndex];
}

KNMusicStoreAlbumModel *KNMusicStoreNetease::albumDetailModel()
{
    //Give back the model.
    return m_albumDetail;
}

void KNMusicStoreNetease::fetchHomeWidgetInfo()
{
    //Update the latest albums.
    m_listThreads[LatestAlbumList]=
            QtConcurrent::run(this,
                              &KNMusicStoreNetease::updateLatestAlbumsList);
    //Update the hot songs list.
    m_listThreads[HotSongList]=
            QtConcurrent::run(this,
                              &KNMusicStoreNetease::updateNeteaseList,
                              m_hotSongModel,
                              QString("3778678"));
    //Update lists.
    m_listThreads[BillboardList]=
            QtConcurrent::run(this,
                              &KNMusicStoreNetease::updateNeteaseList,
                              m_listModel[BillboardList],
                              QString("60198"));
    m_listThreads[iTunesList]=
            QtConcurrent::run(this,
                              &KNMusicStoreNetease::updateNeteaseList,
                              m_listModel[iTunesList],
                              QString("11641012"));
    m_listThreads[OriconList]=
            QtConcurrent::run(this,
                              &KNMusicStoreNetease::updateNeteaseList,
                              m_listModel[OriconList],
                              QString("60131"));
}

void KNMusicStoreNetease::fetchAlbumDetail(const QString &albumId)
{
    //Prepare the rest api.
    QScopedPointer<KNRestApiBase> curl;
    //Initial the rest api.
    curl.reset(new KNRestApiBase);
    //Prepare the response data.
    QByteArray responseData;
    //Get the album detail info.
    if(curl->get(generateNeteaseRequest(QString("http://music.163.com/api/"
                                                "album/%1/").arg(albumId)),
                 responseData,
                 false)==200)
    {
        //Parse the response data to JSON object.
        QJsonObject albumData=QJsonDocument::fromJson(responseData).object();
        //The only important data is the album data.
        if(!albumData.contains("album"))
        {
            //Failed to fetch album data.
            return;
        }
        //Get the album data.
        albumData=albumData.value("album").toObject();
        //Save the information to album model.
        if(m_albumDetail==nullptr)
        {
            //Ignore the settings.
            return;
        }
        //Save the information to album detail model.
        m_albumDetail->setAlbumInfo(KNMusicStoreUtil::AlbumTitle,
                                    albumData.value("name").toString());
        m_albumDetail->setAlbumInfo(KNMusicStoreUtil::AlbumArtist,
                                    albumData.value("artist").toObject()
                                    .value("name").toString());
        m_albumDetail->setAlbumInfo(KNMusicStoreUtil::AlbumReleaseCompany,
                                    albumData.value("company").toString());
        m_albumDetail->setAlbumInfo(
                    KNMusicStoreUtil::AlbumReleaseTime,
                    QDateTime::fromMSecsSinceEpoch(
                        albumData.value("publishTime").toDouble()).toString(
                        "yyyy-MM-dd"));
        //Get the music song data.
        QJsonArray songArray=albumData.value("songs").toArray();
        //Loop for all information.
        for(int i=0; i<songArray.size(); ++i)
        {
            //Get the song object.
            QJsonObject songItem=songArray.at(i).toObject();
            //Generate the song detail info.
            KNMusicStoreUtil::StoreSongItem item;
            //Save the song information.
            item.name=songItem.value("name").toString();
            {
                //Get artist objects.
                QJsonArray songArtists=songItem.value("artists").toArray();
                //Generate artist names list.
                QStringList artistsNames;
                //Get the artist names.
                for(int j=0; j<songArtists.size(); ++j)
                {
                    //Add name to artist names.
                    artistsNames.append(songArtists.at(j).toObject().value(
                                            "name").toString());
                }
                //Combine the artists names.
                item.artist=artistsNames.join(", ");
            }
            item.duration=KNMusicUtil::msecondToString(
                        songItem.value("duration").toDouble());
            item.index=songItem.value("no").toInt();
            //Save the online url for all first.
            QString songUrl=songItem.value("mp3Url").toString();
            item.urlOnline=songUrl;
            item.urlHigh=songUrl;
            item.urlLossless=songUrl;
            //Save the high quality url.
            if(songItem.contains("hMusic"))
            {
                //Get the high quality object.
                QJsonObject highMusic=songItem.value("hMusic").toObject();
                //Get the encrypted id.
                QString songId=QString::number(
                            highMusic.value("dfsId").toDouble()),
                        encId=encryptedId(songId);
                //Then the url will be compose as
                QString highUrl=
                        QString("http://m%1.music.126.net/%2/%3.mp3").arg(
                            QString::number((qrand() % 3)+1),
                            encId,
                            songId);
                //Save the high url for url high and url lossless.
                item.urlHigh=highUrl;
                item.urlLossless=highUrl;
            }
            //Check detail information.
            if(m_albumDetail==nullptr)
            {
                //Ignore the information.
                return;
            }
            //Append item to album detail.
            m_albumDetail->appendItem(item);
        }
        //Download the image.
        if(curl->get(albumData.value("picUrl").toString(),
                     responseData)==200 &&
                m_albumDetail!=nullptr)
        {
            //Save the album data.
            m_albumDetail->setAlbumArt(generateAlbumArt(responseData,
                                                        219, 219));
            //Emit finished signal.
            emit m_albumDetail->albumDetailUpdated();
        }
    }
}

void KNMusicStoreNetease::updateLatestAlbumsList()
{
    //Prepare the rest api.
    QScopedPointer<KNRestApiBase> curl;
    //Initial the rest api.
    curl.reset(new KNRestApiBase);
    //Prepare the response data.
    QByteArray responseData;
    //Get the new albums.
    if(curl->get(generateNeteaseRequest("http://music.163.com/api/album/new?"
                                        "area=ALL&offset=0&total=true&limit=" +
                                        QString::number(ListMaximumSize)),
                 responseData,
                 false)==200)
    {
        //Check pointer first.
        if(m_newAlbumModel==nullptr)
        {
            //If the model is failed, then ignore current work.
            return;
        }
        //Clear the new album model.
        m_newAlbumModel->clear();
        //Get the album list of from the response data.
        QJsonObject albumListData=
                QJsonDocument::fromJson(responseData).object();
        //Get the album list from the album list data object.
        if(albumListData.contains("albums"))
        {
            //Parse the albums.
            QJsonArray albumList=albumListData.value("albums").toArray();
            //Check all items in the array.
            for(auto i=albumList.begin(); i!=albumList.end(); ++i)
            {
                //Translate the item into object.
                QJsonObject albumObject=(*i).toObject();
                //Check the properties are contained or not.
                if(albumObject.contains("name") &&
                        albumObject.contains("picUrl") &&
                        albumObject.contains("artist") &&
                        curl->get(albumObject.value("picUrl").toString(),
                                  responseData)==200)
                {
                    //Construct a album object.
                    KNMusicStoreUtil::StoreAlbumListItem currentAlbum;
                    //Set the album data.
                    currentAlbum.name=albumObject.value("name").toString();
                    currentAlbum.artist=
                            albumObject.value(
                                "artist").toObject().value("name").toString();
                    currentAlbum.albumArt=generateAlbumArt(responseData);
                    currentAlbum.albumData=
                            QString::number(albumObject.value("id").toDouble());
                    //Check pointer first.
                    if(m_newAlbumModel==nullptr)
                    {
                        //If the model is failed, then ignore current work.
                        return;
                    }
                    //Add current album to model.
                    m_newAlbumModel->appendItem(currentAlbum);
                }
            }
        }
    }
}

void KNMusicStoreNetease::updateNeteaseList(
        QPointer<KNMusicStoreAlbumListModel> model,
        const QString &listNo)
{
    //Prepare the rest api.
    QScopedPointer<KNRestApiBase> curl;
    //Initial the rest api.
    curl.reset(new KNRestApiBase);
    //Prepare the response data.
    QByteArray responseData;
    //Get the new hot songs.
    if(curl->get(generateNeteaseRequest("http://music.163.com/discover/toplist?"
                                        "id="+QString(listNo)),
                 responseData,
                 false)==200)
    {
        //Check model first.
        if(model.isNull())
        {
            //Stop to downloading data.
            return;
        }
        //Clear new artist model.
        model.data()->clear();
        //We will parse all the data from the song ids.
        QRegularExpression rex("/song\\?id=(\\d+)");
        QRegularExpressionMatchIterator i=rex.globalMatch(responseData);
        //Generate the song id lists.
        QStringList songIdList;
        //Check whether we could find next item.
        while(i.hasNext())
        {
            //Get the match result.
            QRegularExpressionMatch match=i.next();
            //Add song to song id list.
            songIdList.append(match.captured(1));
        }
        //Get song detail information.
        for(int songCount=ListMaximumSize, i=0;
            i<songIdList.size() && songCount > 0;
            ++i)
        {
            //Get the song detail information.
            QJsonObject songDetail=getSongDetails(curl.data(),
                                                  songIdList.at(i));
            //Check validation.
            if(songDetail.isEmpty())
            {
                //Ignore the current song.
                continue;
            }
            //Check the song data.
            if(songDetail.contains("songs"))
            {
                //Get the songs list
                QJsonArray songsArray=
                        songDetail.value("songs").toArray();
                //Check the size.
                if(songsArray.size()>0)
                {
                    //Get song data.
                    QJsonObject songData=songsArray.at(0).toObject();
                    //Check information and download album data.
                    if(songData.contains("name") &&
                            songData.contains("artists") &&
                            songData.contains("album") &&
                            curl->get(songData.value("album").toObject().value(
                                          "picUrl").toString(),
                                      responseData)==200)
                    {
                        //Reduce the counter.
                        --songCount;
                        //Construct a album object.
                        KNMusicStoreUtil::StoreAlbumListItem currentSong;
                        //Set data.
                        currentSong.name=songData.value("name").toString();
                        currentSong.albumArt=generateAlbumArt(responseData);
                        //Get artist list.
                        QJsonArray artistList=
                                songData.value("artists").toArray();
                        //Get the artist names.
                        QStringList artistNames;
                        for(auto j=0; j<artistList.size(); ++j)
                        {
                            //Add name to artist list.
                            artistNames.append(artistList.at(j).toObject(
                                                   ).value("name").toString());
                        }
                        currentSong.artist=artistNames.join(tr(", "));
                        qDebug()<<songData;
                        //Check model first.
                        if(model.isNull())
                        {
                            //Stop to downloading data.
                            return;
                        }
                        //Add current song to model.
                        model.data()->appendItem(currentSong);
                    }
                }
            }
        }
    }
}

inline QNetworkRequest KNMusicStoreNetease::generateNeteaseRequest(
        const QString &url)
{
    //Generate the network request, initial it with the url.
    QNetworkRequest request;
    //Set default content type header.
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      "application/x-www-form-urlencoded");
    //Set url of the request.
    request.setUrl(QUrl(url));
    //Set to header.
    request.setRawHeader("Cookie", "appver=2.0.2");
    //Set the "Referer" and "Origin".
    request.setRawHeader("Referer", "http://music.163.com/search/");
    request.setRawHeader("Original", "http://music.163.com");
    request.setRawHeader("Connection", "keep-alive");
    request.setRawHeader("Host", "music.163.com");
    request.setRawHeader("User-Agent",
                         "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_9_2) "
                         "AppleWebKit/537.36 (KHTML, like Gecko) "
                         "Chrome/33.0.1750.152 "
                         "Safari/537.36");
    //Give back the request.
    return request;
}

inline QJsonObject KNMusicStoreNetease::getSongDetails(KNRestApiBase *curl,
                                                       const QString &songId)
{
    //Generate the response data.
    QByteArray responseData;
    //Get the song object.
    if(curl->get(generateNeteaseRequest(QString("http://music.163.com/api/song/"
                                                "detail?ids=[%1]").arg(songId)),
                 responseData,
                 false)==200)
    {
        //If we could get the data, the return the json object.
        return QJsonDocument::fromJson(responseData).object();
    }
    //Or else return the null object.
    return QJsonObject();
}

inline QPixmap KNMusicStoreNetease::generateAlbumArt(
        const QByteArray &albumArtData,
        int width,
        int height)
{
    return QPixmap::fromImage(QImage::fromData(albumArtData)).scaled(
                width,
                height,
                Qt::KeepAspectRatio,
                Qt::SmoothTransformation);
}

inline QString KNMusicStoreNetease::timeToText(const double &time)
{
    //Translate the time to string.
    QString timeText=QString::number(time);
}

QString KNMusicStoreNetease::encryptedId(const QString &songId)
{
    //Based on https://github.com/yanunon/NeteaseCloudMusic
    //Translate song id into byte array.
    QByteArray rawId=songId.toLatin1(),
               encryptedData;
    //For all char in song id, do ^ with magic data.
    for(int i=0; i<rawId.size(); ++i)
    {
        //Encrypted the raw id.
        encryptedData.append(rawId.at(i) ^
                             m_magicData.at(i % m_magicDataLength));
    }
    //Do MD5 with the encrypted data. Encode the middle with Base64.
    QByteArray middle=
            QCryptographicHash::hash(encryptedData,
                                     QCryptographicHash::Md5).toBase64();
    //Remove the last byte.
    middle.resize(middle.size()-1);
    //Replace the specific byte.
    middle.replace('/', '_');
    middle.replace('+', '-');
    //Give back the result.
    return middle;
}