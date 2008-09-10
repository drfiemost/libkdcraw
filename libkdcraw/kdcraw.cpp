/* ============================================================
 *
 * This file is a part of kipi-plugins project
 * http://www.kipi-plugins.org
 *
 * Date        : 2006-12-09
 * Description : a tread-safe dcraw program interface
 *
 * Copyright (C) 2006-2008 by Gilles Caulier <caulier dot gilles at gmail dot com> 
 * Copyright (C) 2006-2008 by Marcel Wiesweg <marcel dot wiesweg at gmx dot de>
 * Copyright (C) 2007-2008 by Guillaume Castagnino <casta at xwing dot info>
 *
 * NOTE: Do not use kdDebug() in this implementation because 
 *       it will be multithreaded. Use qDebug() instead. 
 *       See B.K.O #133026 for details.
 *
 * This program is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General
 * Public License as published by the Free Software Foundation;
 * either version 2, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * ============================================================ */

// Qt includes.

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

// LibRaw includes.

#include "libraw/libraw_version.h"

// Local includes.

#include "version.h"
#include "rawfiles.h"
#include "kdcrawprivate.h"
#include "kdcraw.h"
#include "kdcraw.moc"

namespace KDcrawIface
{

KDcraw::KDcraw()
{
    d = new KDcrawPriv(this);
    m_cancel  = false;
}

KDcraw::~KDcraw()
{
    cancel();
    delete d;
}

QString KDcraw::version()
{
    return QString(kdcraw_version);
}

void KDcraw::cancel()
{
    m_cancel = true;
}

bool KDcraw::loadDcrawPreview(QImage& image, const QString& path)
{
    // In first, try to extrcat the embedded JPEG preview. Very fast.
    bool ret = loadEmbeddedPreview(image, path);
    if (ret) return true;

    // In second, decode and half size of RAW picture. More slow.
    return (loadHalfPreview(image, path));
}

bool KDcraw::loadEmbeddedPreview(QImage& image, const QString& path)
{
    QByteArray imgData;

    if ( loadEmbeddedPreview(imgData, path) )
    {
        if (image.loadFromData( imgData ))
        {
            qDebug() << "Using embedded RAW preview extraction";
            return true;
        }
    }

    return false;
}

bool KDcraw::loadEmbeddedPreview(QByteArray& imgData, const QString& path)
{
    QFileInfo fileInfo(path);
    QString   rawFilesExt(rawFiles());
    QString ext = fileInfo.suffix().toUpper();

    if (!fileInfo.exists() || ext.isEmpty() || !rawFilesExt.toUpper().contains(ext))
        return false;

    LibRaw raw;
    int ret = raw.open_file(QFile::encodeName(path));
    if (ret != LIBRAW_SUCCESS)
    {
        qDebug() << "LibRaw: failed to run open_file: " << libraw_strerror(ret) << endl;
        return false;
    }

    ret = raw.unpack_thumb();
    if (ret != LIBRAW_SUCCESS)
    {
        qDebug() << "LibRaw: failed to run unpack_thumb: " << libraw_strerror(ret) << endl;
        return false;
    }

    libraw_processed_image_t *thumb = raw.dcraw_make_mem_thumb(&ret);
    if(!thumb)
    {
        qDebug() << "LibRaw: failed to run dcraw_make_mem_thumb: " << libraw_strerror(ret) << endl;
        return false;
    }

    if(thumb->type == LIBRAW_IMAGE_BITMAP)
        KDcrawPriv::createPPMHeader(imgData, thumb);
    else
        imgData = QByteArray((const char*)thumb->data, (int)thumb->data_size);

    if ( imgData.isEmpty() )
    {
        qDebug("Failed to load JPEG thumb from LibRaw!");
        return false;
    }

    return true;
}

bool KDcraw::loadHalfPreview(QImage& image, const QString& path)
{
    QFileInfo fileInfo(path);
    QString   rawFilesExt(rawFiles());
    QString   ext = fileInfo.suffix().toUpper();

    if (!fileInfo.exists() || ext.isEmpty() || !rawFilesExt.toUpper().contains(ext))
        return false;

    qDebug("Try to use reduced RAW picture extraction");

    LibRaw raw;
    raw.imgdata.params.use_auto_wb   = 1; // Use automatic white balance.
    raw.imgdata.params.use_camera_wb = 1; // Use camera white balance, if possible.
    raw.imgdata.params.half_size     = 1; // Half-size color image (3x faster than -q).

    int ret = raw.open_file(QFile::encodeName(path));
    if (ret != LIBRAW_SUCCESS)
    {
        qDebug() << "LibRaw: failed to run open_file: " << libraw_strerror(ret) << endl;
        return false;
    }

    ret = raw.unpack();
    if (ret != LIBRAW_SUCCESS)
    {
        qDebug() << "LibRaw: failed to run unpack: " << libraw_strerror(ret) << endl;
        return false;
    }

    ret = raw.dcraw_process();
    if (ret != LIBRAW_SUCCESS)
    {
        qDebug() << "LibRaw: failed to run dcraw_process: " << libraw_strerror(ret) << endl;
        return false;
    }

    libraw_processed_image_t *halfImg = raw.dcraw_make_mem_image(&ret);
    if(!halfImg)
    {
        qDebug() << "LibRaw: failed to run dcraw_make_mem_image: " << libraw_strerror(ret) << endl;
        return false;
    }

    QByteArray imgData;
    KDcrawPriv::createPPMHeader(imgData, halfImg);

    if (!image.loadFromData(imgData))
    {
        qDebug("Failed to load PPM data from LibRaw!");
        return false;
    }

    qDebug("Using reduced RAW picture extraction");
    return true;
}

bool KDcraw::rawFileIdentify(DcrawInfoContainer& identify, const QString& path)
{
    QFileInfo fileInfo(path);
    QString   rawFilesExt(rawFiles());
    QString ext          = fileInfo.suffix().toUpper();
    identify.isDecodable = false;

    if (!fileInfo.exists() || ext.isEmpty() || !rawFilesExt.toUpper().contains(ext))
        return false;

    LibRaw raw;

    int ret = raw.open_file(QFile::encodeName(path));
    if (ret != LIBRAW_SUCCESS)
    {
        qDebug() << "LibRaw: failed to run open_file: " << libraw_strerror(ret) << endl;
        return false;
    }

    ret = raw.adjust_sizes_info_only();
    if (ret != LIBRAW_SUCCESS)
    {
        qDebug() << "LibRaw: failed to run adjust_sizes_info_only: " << libraw_strerror(ret) << endl;
        return false;
    }

    identify.dateTime         = QDateTime::fromTime_t(raw.imgdata.other.timestamp);
    identify.make             = QString(raw.imgdata.idata.make);
    identify.model            = QString(raw.imgdata.idata.model);
    identify.owner            = QString(raw.imgdata.other.artist);
    identify.DNGVersion       = QString::number(raw.imgdata.idata.dng_version);
    identify.sensitivity      = raw.imgdata.other.iso_speed;
    identify.exposureTime     = raw.imgdata.other.shutter;
    identify.aperture         = raw.imgdata.other.aperture;
    identify.focalLength      = raw.imgdata.other.focal_len;
    identify.imageSize        = QSize(raw.imgdata.sizes.width, raw.imgdata.sizes.height);
    identify.fullSize         = QSize(raw.imgdata.sizes.raw_width, raw.imgdata.sizes.raw_height);
    identify.outputSize       = QSize(raw.imgdata.sizes.iwidth, raw.imgdata.sizes.iheight);
    identify.thumbSize        = QSize(raw.imgdata.thumbnail.twidth, raw.imgdata.thumbnail.theight);
    identify.hasIccProfile    = raw.imgdata.color.profile ? true : false;
    identify.isDecodable      = true;
    identify.pixelAspectRatio = raw.imgdata.sizes.pixel_aspect;
    identify.rawColors        = raw.imgdata.idata.colors;
    identify.rawImages        = raw.imgdata.idata.raw_count;

    if (raw.imgdata.idata.filters) 
    {
        if (!raw.imgdata.idata.cdesc[3]) raw.imgdata.idata.cdesc[3] = 'G';
        for (int i=0; i < 16; i++)
            identify.filterPattern.append(raw.imgdata.idata.cdesc[raw.fc(i >> 1,i & 1)]);
    }

    for(int c = 0 ; c < raw.imgdata.idata.colors ; c++)
        identify.daylightMult[c] = raw.imgdata.color.pre_mul[c];

    if (raw.imgdata.color.cam_mul[0] > 0) 
    {
        for(int c = 0 ; c < 4 ; c++) 
            identify.cameraMult[c] = raw.imgdata.color.cam_mul[c];
    }

    // NOTE: since dcraw.c 8.77, this information has disapear...
    identify.hasSecondaryPixel = false;

    return true;
}

// ----------------------------------------------------------------------------------

bool KDcraw::decodeHalfRAWImage(const QString& filePath, const RawDecodingSettings& rawDecodingSettings, 
                                QByteArray &imageData, int &width, int &height, int &rgbmax)
{
    m_rawDecodingSettings                    = rawDecodingSettings;
    m_rawDecodingSettings.halfSizeColorImage = true;
    return (loadFromDcraw(filePath, imageData, width, height, rgbmax));
}

bool KDcraw::decodeRAWImage(const QString& filePath, const RawDecodingSettings& rawDecodingSettings, 
                            QByteArray &imageData, int &width, int &height, int &rgbmax)
{
    m_rawDecodingSettings = rawDecodingSettings;
    return (loadFromDcraw(filePath, imageData, width, height, rgbmax));
}

bool KDcraw::checkToCancelWaitingData()
{
    return m_cancel;
}

bool KDcraw::checkToCancelReceivingData()
{
    return m_cancel;
}

void KDcraw::setWaitingDataProgress(double)
{
}

void KDcraw::setReceivingDataProgress(double)
{
}

bool KDcraw::loadFromDcraw(const QString& filePath, QByteArray &imageData, 
                           int &width, int &height, int &rgbmax)
{
    m_cancel = false;

    QStringList args;     // List of dcraw options to show as debug message on the console.
    LibRaw      raw;

    // Set progress call back function.
    raw.set_progress_handler(callbackForLibRaw, &d);

    if (m_rawDecodingSettings.gamma16bit)
    {
        // 16 bits color depth auto-gamma is not implemented in dcraw.
        raw.imgdata.params.gamma_16bit = 1;
    }

    if (m_rawDecodingSettings.sixteenBitsImage)
    {
        // (-4) 16bit ppm output
        args.append("-4");
        raw.imgdata.params.output_bps = 16;
    }

    if (m_rawDecodingSettings.halfSizeColorImage)
    {
        // (-h) Half-size color image (3x faster than -q).
        args.append("-h");
        raw.imgdata.params.half_size = 1;
    }

    if (m_rawDecodingSettings.RGBInterpolate4Colors)
    {
        // (-f) Interpolate RGB as four colors.
        args.append("-f");
        raw.imgdata.params.four_color_rgb = 1;
    }

    if (m_rawDecodingSettings.DontStretchPixels)
    {
        // (-j) Do not stretch the image to its correct aspect ratio.
        args.append("-j");
        raw.imgdata.params.use_fuji_rotate = 1;
    }

    // (-H) Unclip highlight color.
    args.append(QString("-H %1").arg(m_rawDecodingSettings.unclipColors));
    raw.imgdata.params.highlight = m_rawDecodingSettings.unclipColors;


    if (m_rawDecodingSettings.brightness != 1.0)
    {
        // (-b) Set Brightness value.
        args.append(QString("-b %1").arg(m_rawDecodingSettings.brightness));
        raw.imgdata.params.bright = m_rawDecodingSettings.brightness;
    }

    if (m_rawDecodingSettings.enableBlackPoint)
    {
        // (-k) Set Black Point value.
        args.append(QString("-k %1").arg(m_rawDecodingSettings.blackPoint));
        raw.imgdata.params.user_black = m_rawDecodingSettings.blackPoint;
    }

    if (m_rawDecodingSettings.enableWhitePoint)
    {
        // (-S) Set White Point value (saturation).
        args.append(QString("-S %1").arg(m_rawDecodingSettings.whitePoint));
        raw.imgdata.params.user_sat = m_rawDecodingSettings.whitePoint;
    }

    if (m_rawDecodingSettings.medianFilterPasses > 0)
    {
        // (-m) After interpolation, clean up color artifacts by repeatedly applying a 3x3 median filter to the R-G and B-G channels.
        args.append(QString("-m %1").arg(m_rawDecodingSettings.medianFilterPasses));
        raw.imgdata.params.med_passes = m_rawDecodingSettings.medianFilterPasses;
    }

    if (!m_rawDecodingSettings.deadPixelMap.isEmpty())
    {
        // (-P) Read the dead pixel list from this file.
        args.append(QString("-P %1").arg(m_rawDecodingSettings.deadPixelMap));
        raw.imgdata.params.bad_pixels = QFile::encodeName(m_rawDecodingSettings.deadPixelMap).data();
    }

    switch (m_rawDecodingSettings.whiteBalance)
    {
        case RawDecodingSettings::NONE:
        {
            break;
        }
        case RawDecodingSettings::CAMERA:
        {
            // (-w) Use camera white balance, if possible.
            args.append("-w");
            raw.imgdata.params.use_camera_wb = 1;
            break;
        }
        case RawDecodingSettings::AUTO:
        {
            // (-a) Use automatic white balance.
            args.append("-a");
            raw.imgdata.params.use_auto_wb = 1;
            break;
        }
        case RawDecodingSettings::CUSTOM:
        {
            /* Convert between Temperature and RGB.
             */
            double T;
            double RGB[3];
            double xD, yD, X, Y, Z;
            DcrawInfoContainer identify;
            T = m_rawDecodingSettings.customWhiteBalance;

            /* Here starts the code picked and adapted from ufraw (0.12.1)
               to convert Temperature + green multiplier to RGB multipliers
            */
            /* Convert between Temperature and RGB.
             * Base on information from http://www.brucelindbloom.com/
             * The fit for D-illuminant between 4000K and 12000K are from CIE
             * The generalization to 2000K < T < 4000K and the blackbody fits
             * are my own and should be taken with a grain of salt.
             */
            const double XYZ_to_RGB[3][3] = {
                { 3.24071,  -0.969258,  0.0556352 },
                {-1.53726,  1.87599,    -0.203996 },
                {-0.498571, 0.0415557,  1.05707 } };
            // Fit for CIE Daylight illuminant
            if (T <= 4000)
            {
                xD = 0.27475e9/(T*T*T) - 0.98598e6/(T*T) + 1.17444e3/T + 0.145986;
            }
            else if (T <= 7000)
            {
                xD = -4.6070e9/(T*T*T) + 2.9678e6/(T*T) + 0.09911e3/T + 0.244063;
            }
            else
            {
                xD = -2.0064e9/(T*T*T) + 1.9018e6/(T*T) + 0.24748e3/T + 0.237040;
            }

            yD     = -3*xD*xD + 2.87*xD - 0.275;
            X      = xD/yD;
            Y      = 1;
            Z      = (1-xD-yD)/yD;
            RGB[0] = X*XYZ_to_RGB[0][0] + Y*XYZ_to_RGB[1][0] + Z*XYZ_to_RGB[2][0];
            RGB[1] = X*XYZ_to_RGB[0][1] + Y*XYZ_to_RGB[1][1] + Z*XYZ_to_RGB[2][1];
            RGB[2] = X*XYZ_to_RGB[0][2] + Y*XYZ_to_RGB[1][2] + Z*XYZ_to_RGB[2][2];
            /* End of the code picked to ufraw
            */

            RGB[1] = RGB[1] / m_rawDecodingSettings.customWhiteBalanceGreen;

            /* By default, decraw override his default D65 WB
               We need to keep it as a basis : if not, colors with some
               DSLR will have a high dominant of color that will lead to
               a completly wrong WB
            */
            if (rawFileIdentify (identify, filePath))
            {
                RGB[0] = identify.daylightMult[0] / RGB[0];
                RGB[1] = identify.daylightMult[1] / RGB[1];
                RGB[2] = identify.daylightMult[2] / RGB[2];
            }
            else
            {
                RGB[0] = 1.0 / RGB[0];
                RGB[1] = 1.0 / RGB[1];
                RGB[2] = 1.0 / RGB[2];
                qDebug("Warning: cannot get daylight multipliers");
            }

            // (-r) set Raw Color Balance Multipliers.
            raw.imgdata.params.user_mul[0] = RGB[0];
            raw.imgdata.params.user_mul[1] = RGB[1];
            raw.imgdata.params.user_mul[2] = RGB[2];
            raw.imgdata.params.user_mul[3] = RGB[1];
            args.append(QString("-r %1 %2 %3 %4").arg(raw.imgdata.params.user_mul[0])
                                                 .arg(raw.imgdata.params.user_mul[1])
                                                 .arg(raw.imgdata.params.user_mul[2])
                                                 .arg(raw.imgdata.params.user_mul[3]));
            break;
        }
        case RawDecodingSettings::AERA:
        {
            // (-A) Calculate the white balance by averaging a rectangular area from image.
            raw.imgdata.params.greybox[0] = m_rawDecodingSettings.whiteBalanceArea.left();
            raw.imgdata.params.greybox[1] = m_rawDecodingSettings.whiteBalanceArea.top();
            raw.imgdata.params.greybox[2] = m_rawDecodingSettings.whiteBalanceArea.width();
            raw.imgdata.params.greybox[3] = m_rawDecodingSettings.whiteBalanceArea.height();
            args.append(QString("-A %1 %2 %3 %4").arg(raw.imgdata.params.greybox[0])
                                                 .arg(raw.imgdata.params.greybox[1])
                                                 .arg(raw.imgdata.params.greybox[2])
                                                 .arg(raw.imgdata.params.greybox[3]));
            break;
        }
    }

    // (-q) Use an interpolation method.
    raw.imgdata.params.user_qual = m_rawDecodingSettings.RAWQuality;
    args.append(QString("-q %1").arg(m_rawDecodingSettings.RAWQuality));

    if (m_rawDecodingSettings.enableNoiseReduction)
    {
        // (-n) Use wavelets to erase noise while preserving real detail.
        args.append(QString("-n %1").arg(m_rawDecodingSettings.NRThreshold));
        raw.imgdata.params.threshold = m_rawDecodingSettings.NRThreshold;
    }

    if (m_rawDecodingSettings.enableCACorrection)
    {
        // (-C) Set Correct chromatic aberration correction.
        raw.imgdata.params.aber[0] = m_rawDecodingSettings.caMultiplier[0];
        raw.imgdata.params.aber[2] = m_rawDecodingSettings.caMultiplier[1];
        args.append(QString("-C %1 %2").arg(raw.imgdata.params.aber[0])
                                       .arg(raw.imgdata.params.aber[2]));
    }

    switch (m_rawDecodingSettings.inputColorSpace)
    {
        case RawDecodingSettings::EMBEDDED:
        {
            // (-p embed) Use input profile from RAW file to define the camera's raw colorspace.
            raw.imgdata.params.camera_profile = QString("embed").toAscii().data();
            args.append(QString("-p embed"));
            break;
        }
        case RawDecodingSettings::CUSTOMINPUTCS:
        {
            if (!m_rawDecodingSettings.inputProfile.isEmpty())
            {
                // (-p) Use input profile file to define the camera's raw colorspace.
                raw.imgdata.params.camera_profile = QFile::encodeName(m_rawDecodingSettings.inputProfile).data();
                args.append(QString("-p %1").arg(raw.imgdata.params.camera_profile));
            }
            break;
        }
        default:   // No input profile
            break;
    }

    switch (m_rawDecodingSettings.outputColorSpace)
    {
        case RawDecodingSettings::CUSTOMOUTPUTCS:
        {
            if (!m_rawDecodingSettings.outputProfile.isEmpty())
            {
                // (-o) Use ICC profile file to define the output colorspace.
                raw.imgdata.params.output_profile = QFile::encodeName(m_rawDecodingSettings.outputProfile).data();
                args.append(QString("-o %1").arg(raw.imgdata.params.output_profile));
            }
            break;
        }
        default:
        {
            // (-o) Define the output colorspace.
            args.append(QString("-o %1").arg(m_rawDecodingSettings.outputColorSpace));
            raw.imgdata.params.output_color = m_rawDecodingSettings.outputColorSpace;
            break;
        }
    }

    setReceivingDataProgress(0.1);

    args.append(filePath);
    qDebug() << "LibRaw dcraw options: " << args << endl;

    int ret = raw.open_file(QFile::encodeName(filePath));
    if (ret != LIBRAW_SUCCESS)
    {
        qDebug() << "LibRaw: failed to run open_file: " << libraw_strerror(ret) << endl;
        return false;
    }

    setReceivingDataProgress(0.2);

    ret = raw.unpack();
    if (ret != LIBRAW_SUCCESS)
    {
        qDebug() << "LibRaw: failed to run unpack: " << libraw_strerror(ret) << endl;
        return false;
    }

    if (m_cancel) return false;
    setReceivingDataProgress(0.25);

    ret = raw.dcraw_process();
    if (ret != LIBRAW_SUCCESS)
    {
        qDebug() << "LibRaw: failed to run dcraw_process: " << libraw_strerror(ret) << endl;
        return false;
    }

    if (m_cancel) return false;
    setReceivingDataProgress(0.3);

    libraw_processed_image_t *img = raw.dcraw_make_mem_image(&ret);
    if(!img)
    {
        qDebug() << "LibRaw: failed to run dcraw_make_mem_image: " << libraw_strerror(ret) << endl;
        return false;
    }

    if (m_cancel) return false;
    setReceivingDataProgress(0.35);

    width     = img->width;
    height    = img->height;
    rgbmax    = (1 << img->bits)-1;
    imageData = QByteArray((const char*)img->data, (int)img->data_size);

    if (m_cancel) return false;
    setReceivingDataProgress(0.4);

    qDebug("Raw data info: width %i height %i rgbmax %i", width, height, rgbmax);

    return true;
}

const char *KDcraw::rawFiles()
{
    return raw_file_extentions;
}

QStringList KDcraw::rawFilesList()
{
    QString string = QString::fromLatin1(rawFiles());
    return string.remove("*.").split(' ');
}

int KDcraw::rawFilesVersion()
{
    return raw_file_extensions_version;
}

QStringList KDcraw::supportedCamera()
{
    QStringList camera;
    const char** list = LibRaw::cameraList();
    for (int i = 0; i < LibRaw::cameraCount(); i++)
        camera.append(list[i]);

    return camera;
}

QString KDcraw::librawVersion()
{
    return QString(LIBRAW_VERSION_STR);
}

}  // namespace KDcrawIface
