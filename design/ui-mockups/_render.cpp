#include <QApplication>
#include <QUiLoader>
#include <QFile>
#include <QWidget>
#include <QPalette>
#include <QStyleFactory>
int main(int argc, char** argv){
  QApplication app(argc, argv);
  // 3. arg == "dark" → sötét Fusion paletta (Designer sötét-téma preview szimulálása)
  if(argc>3 && QString(argv[3])=="dark"){
    app.setStyle(QStyleFactory::create("Fusion"));
    QPalette p;
    p.setColor(QPalette::Window, QColor(38,38,40));
    p.setColor(QPalette::WindowText, QColor(222,222,224));
    p.setColor(QPalette::Base, QColor(28,28,30));
    p.setColor(QPalette::AlternateBase, QColor(48,48,52));
    p.setColor(QPalette::Text, QColor(222,222,224));
    p.setColor(QPalette::Button, QColor(56,56,60));
    p.setColor(QPalette::ButtonText, QColor(222,222,224));
    p.setColor(QPalette::Highlight, QColor(54,122,204));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Mid, QColor(150,150,154));
    p.setColor(QPalette::Midlight, QColor(86,86,90));
    p.setColor(QPalette::Link, QColor(96,166,250));
    app.setPalette(p);
  }
  QFile f(argv[1]);
  if(!f.open(QIODevice::ReadOnly)) return 3;
  QUiLoader loader;
  QWidget* w = loader.load(&f);
  f.close();
  if(!w) return 2;
  { QSize g=w->geometry().size(); QSize h=w->sizeHint(); w->resize(QSize(qMax(g.width(),h.width()), qMax(g.height(),h.height()))); }
  w->show();
  app.processEvents(); app.processEvents();
  w->grab().save(argv[2]);
  return 0;
}
