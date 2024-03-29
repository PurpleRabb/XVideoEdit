#include "mainwindow.h"
#include "ui_XVideoEditUI.h"
#include "xvideothread.h"
#include "xvideofilter.h"
#include <QDebug>
#include <QFileDialog>
#include <QMessageBox>

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    this->setWindowFlag(Qt::FramelessWindowHint);

    qRegisterMetaType<cv::Mat>("cv::Mat");
    connect(XVideoThread::Instance(),SIGNAL(setImage(cv::Mat)),ui->src_widget,SLOT(updateImage(cv::Mat)));
    connect(XVideoThread::Instance(),SIGNAL(setMatImage(cv::Mat)),ui->mat_widget,SLOT(updateImage(cv::Mat)));
    connect(XVideoThread::Instance(),SIGNAL(setBlendImage(cv::Mat)),ui->dst_widget,SLOT(updateImage(cv::Mat)));
    connect(XVideoThread::Instance(),SIGNAL(exportStopped()),this,SLOT(exportStopped()));
    connect(XVideoThread::Instance(),SIGNAL(startPlay(bool)),ui->src_widget,SLOT(pause_play(bool)));
    connect(ui->src_widget,SIGNAL(play_pause(bool)),XVideoThread::Instance(),SLOT(play(bool)));
    ui->resize_widthEdit->setValidator(new QIntValidator(50,3000,this));
    ui->resize_heightEdit->setValidator(new QIntValidator(50,3000,this));
    startTimer(500);
}

MainWindow::~MainWindow()
{
    qDebug() << "~MainWindow";
    delete ui;
}

void MainWindow::Open()
{
    QString filename = QFileDialog::getOpenFileName(this,QString::fromLocal8Bit("请选择视频文件："));
    if(filename.isEmpty())
    {
        return;
    }
    if(!XVideoThread::Instance()->open(filename))
    {
        QMessageBox::information(this,"","open failed!" + filename);
    }
}

void MainWindow::timerEvent(QTimerEvent *event)
{
    //qDebug() << XVideoThread::Instance()->getPlayPos();
    if(!sliderPause)
        ui->srcSlider->setValue(XVideoThread::Instance()->getPlayPos()*1000);
}

void MainWindow::sliderMoved(int value)
{
    //qDebug() << value;
    XVideoThread::Instance()->seek(value);
}

void MainWindow::sliderPressed()
{
    //qDebug() << "slider pressed";
    sliderPause = true;
}

void MainWindow::sliderReleased()
{
    //qDebug() << "slider released";
    sliderPause = false;
}

void MainWindow::setFilter() //设置按钮触发
{
    _cliped = false;//reset flag
    _gray = false;
    XVideoFilter::Instance()->Clear();

    /***对比度亮度调节-->***/
    if(ui->brightSpinBox->value() >= 0 && ui->contrastSpinBox->value() >= 1)
    {
        XVideoFilter::Instance()->Add(XTask{TASK_GAIN,{(double)ui->brightSpinBox->value(),ui->contrastSpinBox->value()}});
    }
    /***<--对比度亮度调节***/

    XVideoFilter::Instance()->Add(XTask{TASK_ROTATE,{(double)ui->rotateBox->currentIndex()}}); //旋转过滤器
    XVideoFilter::Instance()->Add(XTask{TASK_FLIP,{(double)ui->flipBox->currentIndex()}}); //镜像过滤器

    /***尺寸调整-->***/
    if(ui->resize_widthEdit->text().toDouble() >0 && ui->resize_heightEdit->text().toDouble()>0)
    {
        XVideoFilter::Instance()->Add(XTask{TASK_RESIZE,
                                            {ui->resize_widthEdit->text().toDouble(),
                                            ui->resize_heightEdit->text().toDouble()}});
    }
    /***<--尺寸调整***/

    /***裁剪过滤器-->***/
    double clip_x = ui->x_spinBox->value();
    double clip_y = ui->y_spinBox->value();
    double clip_width = ui->w_spinBox->value();
    double clip_height = ui->h_spinBox->value();
    cv::Size _size = XVideoThread::Instance()->getSrcSize();
    if(clip_x >= 0 && clip_y >= 0 && clip_width > 0
            && clip_width < _size.width && clip_height > 0 && clip_height < _size.height)
    {
        _cliped = true;
        XVideoFilter::Instance()->Add(XTask{TASK_CLIP,{clip_x,clip_y,clip_width,clip_height}});
    }
    /***<--裁剪过滤器***/

    /***文字水印-->***/
    if(!ui->textEdit->text().isEmpty())
    {
        int _x = ui->fontxSpinBox->value();
        int _y = ui->fontySpinBox->value();
        int _r = ui->r_spinBox->value();
        int _g = ui->g_spinBox->value();
        int _b = ui->b_spinBox->value();
        int _fx = ui->fontsizeSpinBox->value();
        XVideoFilter::Instance()->setText(ui->textEdit->text(),QPoint(_x,_y),QColor(_r,_g,_b),_fx);
        XVideoFilter::Instance()->Add(XTask{TASK_TEXT,{}});
    }
    /***<--文字水印***/

    /***灰度-->***/
    if(ui->videoColorBox->currentIndex() == 1)
    {
        _gray = true;
    }
    XVideoFilter::Instance()->Add(XTask{TASK_GRAY,{(double)ui->videoColorBox->currentIndex()}});
    /***<--灰度***/

    /***图片水印-->***/
    XVideoFilter::Instance()->Add(XTask{TASK_MASK,{(double)ui->maskX->value(),(double)ui->maskY->value(),ui->maskAlpha->value()}});
    /***<--图片水印***/

    /***视频融合-->***/
    if(_blend)
    {
        qDebug() << "filter blend";
        XVideoFilter::Instance()->Add(XTask{TASK_BLEND,{ui->blendAlpha->value()}});
    }
    /***<--视频融合***/

    /***视频合并-->***/
    if(_merge)
    {
        XVideoFilter::Instance()->Add(XTask{TASK_MERGE,{}});
    }
    /***<--视频合并***/
}

void MainWindow::exportVideo()
{
    if(isExporting)
    {
        isExporting = false;
        XVideoThread::Instance()->stopSave();
        ui->exportButton->setText(tr("开始导出"));
        return;
    }
    QString name = QFileDialog::getSaveFileName(this,"save","out1.avi");
    if(name.isEmpty())
        return;
    int __width = ui->resize_widthEdit->text().toInt();
    int __height = ui->resize_heightEdit->text().toInt();
    if(_cliped)
    {
        __width = ui->w_spinBox->value();
        __height = ui->h_spinBox->value();
    }
    if(_merge)
    {
        cv::Size src1 = XVideoThread::Instance()->getSrcSize();
        cv::Size src2 = XVideoThread::Instance()->getSrc2Size();
        __height = src1.height;
        //src1.rows*((double)src2.cols/(double)src2.rows)
        __width = src1.height*((double)src2.width/(double)src2.height) + src1.width;
    }
    if(XVideoThread::Instance()->startSave(name,__width,__height,!_gray)) //注意这里要传递宽高，否则导出的尺寸不对将无法播放
    {
        isExporting = true;
        ui->exportButton->setText(tr("停止导出"));
    }
}

void MainWindow::exportStopped()
{
    isExporting = false;
    //XVideoThread::Instance()->stopSave();
    ui->exportButton->setText(tr("开始导出"));
}

void MainWindow::mousePressEvent(QMouseEvent *event)
{
    if(event->button() == Qt::LeftButton)
    {
        _dragging = true;
        _startPosition = event->globalPos();
        _framePosition = frameGeometry().topLeft();
    }
}

void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    if(event->buttons() & Qt::LeftButton)
    {
        if(_dragging)
        {
            //相对偏移量
            QPoint delta = event->globalPos() - _startPosition;
            //新位置：窗体原始位置+偏移量
            move(_framePosition + delta);
        }
    }
    QWidget::mouseMoveEvent(event);
}

void MainWindow::mouseReleaseEvent(QMouseEvent *event)
{
    _dragging = false;
    QWidget::mouseReleaseEvent(event);
}

void MainWindow::setMask()
{
    qDebug() << "setMask";
    QString name = QFileDialog::getOpenFileName(this,
                                                QString::fromLocal8Bit("请选择水印图片："), "", tr("Image Files (*.png *.jpg *.bmp)"));
    if(name.isEmpty())
        return;
    XVideoThread::Instance()->setMask(name);
}

void MainWindow::openBlendVideo()
{
    qDebug() << "openBlendVideo()";
    QString filename = QFileDialog::getOpenFileName(this,QString::fromLocal8Bit("选择合并视频文件："));
    if(filename.isEmpty())
    {
        return;
    }
    if(!XVideoThread::Instance()->openBlend(filename))
    {
        QMessageBox::information(this,"","open failed!" + filename);
    }
    _blendVideoOpened = true;
}

void MainWindow::startBlend()
{
    //XVideoThread::Instance()->startBlend(!_blend);
    if(!_blendVideoOpened)
        return;
    if(!_blend)
    {
        _blend = true;
        ui->btnBlend->setText(tr("融合中"));
        _merge = false;
        ui->btnMerge->setText(tr("合并视频"));
    }
    else
    {
        _blend = false;
        ui->btnBlend->setText(tr("视频融合"));
    }
}

void MainWindow::startMerge()
{
    if(!_blendVideoOpened)
        return;
    if(!_merge)
    {
        _merge = true;
        ui->btnMerge->setText(tr("合并中"));
        _blend = false;
        ui->btnBlend->setText(tr("视频融合"));
    }
    else
    {
        _merge = false;
        ui->btnMerge->setText(tr("合并视频"));
    }
}
