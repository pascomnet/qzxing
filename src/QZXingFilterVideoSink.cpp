#include "QZXingFilterVideoSink.h"
#include <QDebug>
#include <QtConcurrent/QtConcurrent>
#include "QZXingImageProvider.h"

QZXingFilter::QZXingFilter(QObject *parent)
    : QObject(parent)
    , decoder(QZXing::DecoderFormat_QR_CODE)
    , decoding(false)
{
    /// Connecting signals to handlers that will send signals to QML
    connect(&decoder, &QZXing::decodingStarted,
            this, &QZXingFilter::handleDecodingStarted);
    connect(&decoder, &QZXing::decodingFinished,
            this, &QZXingFilter::handleDecodingFinished);
}

QZXingFilter::~QZXingFilter()
{
    if(!processThread.isFinished()) {
        processThread.cancel();
        processThread.waitForFinished();
    }
}

void QZXingFilter::handleDecodingStarted()
{
    decoding = true;
    emit decodingStarted();
    emit isDecodingChanged();
}

void QZXingFilter::handleDecodingFinished(bool succeeded)
{
    decoding = false;
    emit decodingFinished(succeeded, decoder.getProcessTimeOfLastDecoding());
    emit isDecodingChanged();
}

void QZXingFilter::setOrientation(int orientation)
{
    if (orientation_ == orientation) {
            return;
        }

        orientation_ = orientation;
        emit orientationChanged(orientation_);
}

int QZXingFilter::orientation() const
{
    return orientation_;
}

void QZXingFilter::setVideoSink(QObject *videoSink){
    m_videoSink = qobject_cast<QVideoSink*>(videoSink);

    connect(m_videoSink, &QVideoSink::videoFrameChanged, this, &QZXingFilter::processFrame, Qt::DirectConnection);
}

void QZXingFilter::processFrame(const QVideoFrame &f) {
    // Cleaned up version of https://github.com/CMGeorge/qzxing/commit/10b917f071b1f5b0145f04bb10ec8404d314165e#diff-cccfcad0b6ba438f5e4e6219f041e86fa651ea64921b2ba25ed5703af20c879f
    if(!isDecoding() && processThread.isFinished()){
        decoding = true;
        bool didWeMapTheFrame = false;
        QVideoFrame frame{f};
        if (!frame.isMapped()) {
#ifdef Q_OS_ANDROID
            m_videoSink->setRhi(nullptr); // https://bugreports.qt.io/browse/QTBUG-97789
#endif
            didWeMapTheFrame = frame.map(QVideoFrame::ReadOnly);
        }

        if (frame.isMapped()) {
            QImage image = frame.toImage();
            if (!image.isNull()) {
                processThread = QtConcurrent::run([=]() {
                    QImage frameToProcess{image};
                    const QRect &rect = captureRect.toRect();

                    if (captureRect.isValid() && frameToProcess.size() != rect.size()) {
                        frameToProcess = image.copy(rect);
                    }

                    if (!orientation_) {
                        decoder.decodeImage(frameToProcess);
                    } else {
                        QTransform transformation;
                        transformation.translate(frameToProcess.rect().center().x(), frameToProcess.rect().center().y());
                        transformation.rotate(-orientation_);
                        QImage translatedImage = frameToProcess.transformed(transformation);
                        decoder.decodeImage(translatedImage);
                    }

                    decoder.decodeImage(frameToProcess, frameToProcess.width(), frameToProcess.height());
                    decoding = false;
                });
            } else {
                decoding = false;
            }
            if(didWeMapTheFrame) {
                frame.unmap();
            }
        } else {
            decoding = false;
        }
    }
}
