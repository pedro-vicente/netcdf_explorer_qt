//Copyright (C) 2016 Pedro Vicente
//GNU General Public License (GPL) Version 3 described in the LICENSE file 
//sample OPeNDAP data
//http://www.esrl.noaa.gov/psd/thredds/dodsC/Datasets/cmap/enh/precip.mon.mean.nc

#include <QApplication>
#include <QMetaType>
#include <cassert>
#include <vector>
#include <algorithm>
#include "netcdf_explorer.hpp"

const char* get_format(const nc_type typ);
static const char app_name[] = "netCDF Explorer";

/////////////////////////////////////////////////////////////////////////////////////////////////////
//main
/////////////////////////////////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[])
{
  Q_INIT_RESOURCE(netcdf_explorer);
  QApplication app(argc, argv);
  QCoreApplication::setApplicationVersion("1.1");
  QCoreApplication::setApplicationName("netCDF Explorer");
#if QT_VERSION >= 0x050000
  QCommandLineParser parser;
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addPositionalArgument("file", "The file to open.");
  parser.process(app);
  const QStringList args = parser.positionalArguments();
#endif

  MainWindow window;
#if QT_VERSION >= 0x050000
  if(args.size())
  {
    QString file_name = args.at(0);
    window.read_file(file_name);
  }
#endif
  window.showMaximized();
  return app.exec();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//ncdata_t
//ncdata_t is an abstraction to store in memory information for both: 1) netCDF variables. 2) netCDF attributes
//1) netCDF variables
//a netCDF variable has a name, a netCDF type, data buffer, and an array of dimensions
//these are defined in iteration of the file
//the data buffer is stored on per load variable from tree using netCDF API from item input
//2) netCDF attributes
//a netCDF attribute has a name, a netCDF type, data buffer, and a size
//Variables may be multidimensional. Attributes are all either scalars(single-valued) or vectors(a single, fixed dimension)
/////////////////////////////////////////////////////////////////////////////////////////////////////

class ncdata_t
{
public:
  ncdata_t(const char* name, nc_type nc_typ, const std::vector<size_t> &dim) :
    m_name(name),
    m_nc_type(nc_typ),
    m_buf(NULL),
    m_dim(dim)
  {
  }
  ~ncdata_t()
  {
    switch(m_nc_type)
    {
    case NC_STRING:
      if(m_buf)
      {
        char **buf_string = NULL;
        size_t idx_buf = 0;
        buf_string = static_cast<char**> (m_buf);
        if(m_dim.size())
        {
          for(size_t idx_dmn = 0; idx_dmn < m_dim.size(); idx_dmn++)
          {
            for(size_t idx_sz = 0; idx_sz < m_dim[idx_dmn]; idx_sz++)
            {
              free(buf_string[idx_buf]);
              idx_buf++;
            }
          }
        }
        else
        {
          free(*buf_string);
        }
        free(static_cast<char**>(buf_string));
      }
      break;
    default:
      free(m_buf);
    }
  }
  void store(void *buf)
  {
    m_buf = buf;
  }
  std::string m_name;
  nc_type m_nc_type;
  void *m_buf;
  std::vector<size_t> m_dim;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////
//ItemData
/////////////////////////////////////////////////////////////////////////////////////////////////////

class ItemData
{
public:
  enum ItemKind
  {
    Root,
    Group,
    Variable,
    Attribute
  };

  ItemData(ItemKind kind, const std::string& file_name, const std::string& grp_nm_fll, const std::string& item_nm,
    ItemData *item_data_prn, ncdata_t *ncdata) :
    m_file_name(file_name),
    m_grp_nm_fll(grp_nm_fll),
    m_item_nm(item_nm),
    m_kind(kind),
    m_item_data_prn(item_data_prn),
    m_ncdata(ncdata)
  {
  }
  ~ItemData()
  {
    delete m_ncdata;
    for(size_t idx_dmn = 0; idx_dmn < m_ncvar_crd.size(); idx_dmn++)
    {
      delete m_ncvar_crd[idx_dmn];
    }
  }
  std::string m_file_name;  // (Root/Variable/Group/Attribute) file name
  std::string m_grp_nm_fll; // (Group) full name of group
  std::string m_item_nm; // (Root/Variable/Group/Attribute ) item name to display on tree
  ItemKind m_kind; // (Root/Variable/Group/Attribute) type of item 
  std::vector<std::string> m_var_nms; // (Group) list of variables if item is group (filled in file iteration)
  ItemData *m_item_data_prn; //  (Variable/Group) item data of the parent group (to get list of variables in group)
  ncdata_t *m_ncdata; // (Variable, Attribute) netCDF variable/attribute to display
  std::vector<ncdata_t *> m_ncvar_crd; // (Variable) optional coordinate variables for variable
};

Q_DECLARE_METATYPE(ItemData*);

/////////////////////////////////////////////////////////////////////////////////////////////////////
//get_item_data
/////////////////////////////////////////////////////////////////////////////////////////////////////

ItemData* get_item_data(QTreeWidgetItem *item)
{
  QVariant data = item->data(0, Qt::UserRole);
  ItemData *item_data = data.value<ItemData*>();
  return item_data;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//MainWindow::MainWindow
/////////////////////////////////////////////////////////////////////////////////////////////////////

MainWindow::MainWindow()
{
  ///////////////////////////////////////////////////////////////////////////////////////
  //mdi area
  ///////////////////////////////////////////////////////////////////////////////////////

  m_mdi_area = new QMdiArea;
  setCentralWidget(m_mdi_area);

  setWindowTitle(tr("netCDF Explorer"));

  ///////////////////////////////////////////////////////////////////////////////////////
  //status bar
  ///////////////////////////////////////////////////////////////////////////////////////

  statusBar()->showMessage(tr("Ready"));

  ///////////////////////////////////////////////////////////////////////////////////////
  //dock for tree
  ///////////////////////////////////////////////////////////////////////////////////////

  m_tree_dock = new QDockWidget(this);
  m_tree_dock->setFeatures(QDockWidget::NoDockWidgetFeatures);

  ///////////////////////////////////////////////////////////////////////////////////////
  //browser tree
  ///////////////////////////////////////////////////////////////////////////////////////

  m_tree = new FileTreeWidget();
  m_tree->setHeaderHidden(1);
  m_tree->set_main_window(this);
  //add dock
  m_tree_dock->setWidget(m_tree);
  addDockWidget(Qt::LeftDockWidgetArea, m_tree_dock);

  ///////////////////////////////////////////////////////////////////////////////////////
  //actions
  ///////////////////////////////////////////////////////////////////////////////////////

  ///////////////////////////////////////////////////////////////////////////////////////
  //open
  ///////////////////////////////////////////////////////////////////////////////////////

  m_action_open = new QAction(tr("&Open..."), this);
  m_action_open->setIcon(QIcon(":/images/open.png"));
  m_action_open->setShortcut(QKeySequence::Open);
  m_action_open->setStatusTip(tr("Open a file"));
  connect(m_action_open, SIGNAL(triggered()), this, SLOT(open_file()));

  ///////////////////////////////////////////////////////////////////////////////////////
  //open_dap
  ///////////////////////////////////////////////////////////////////////////////////////

  m_action_opendap = new QAction(tr("OPeN&DAP..."), this);
  m_action_opendap->setIcon(QIcon(":/images/open_dap.png"));
  m_action_opendap->setStatusTip(tr("Open a OpenDap URL file"));
  connect(m_action_opendap, SIGNAL(triggered()), this, SLOT(open_dap()));

  ///////////////////////////////////////////////////////////////////////////////////////
  //exit
  ///////////////////////////////////////////////////////////////////////////////////////

  m_action_exit = new QAction(tr("E&xit"), this);
  m_action_exit->setShortcut(tr("Ctrl+Q"));
  m_action_exit->setStatusTip(tr("Exit the application"));
  connect(m_action_exit, SIGNAL(triggered()), this, SLOT(close()));

  ///////////////////////////////////////////////////////////////////////////////////////
  //windows
  ///////////////////////////////////////////////////////////////////////////////////////

  m_action_close_all = new QAction(tr("Close &All"), this);
  m_action_close_all->setStatusTip(tr("Close all the windows"));
  connect(m_action_close_all, SIGNAL(triggered()), m_mdi_area, SLOT(closeAllSubWindows()));

  m_action_tile = new QAction(tr("&Tile"), this);
  m_action_tile->setStatusTip(tr("Tile the windows"));
  connect(m_action_tile, SIGNAL(triggered()), m_mdi_area, SLOT(tileSubWindows()));

  ///////////////////////////////////////////////////////////////////////////////////////
  //about
  ///////////////////////////////////////////////////////////////////////////////////////

  m_action_about = new QAction(tr("&About"), this);
  m_action_about->setStatusTip(tr("Show the application's About box"));
  connect(m_action_about, SIGNAL(triggered()), this, SLOT(about()));

  ///////////////////////////////////////////////////////////////////////////////////////
  //recent files
  ///////////////////////////////////////////////////////////////////////////////////////

  for(int i = 0; i < max_recent_files; ++i)
  {
    m_action_recent_file[i] = new QAction(this);
    m_action_recent_file[i]->setVisible(false);
    connect(m_action_recent_file[i], SIGNAL(triggered()), this, SLOT(open_recent_file()));
  }

  ///////////////////////////////////////////////////////////////////////////////////////
  //menus
  ///////////////////////////////////////////////////////////////////////////////////////

  m_menu_file = menuBar()->addMenu(tr("&File"));
  m_menu_file->addAction(m_action_open);
  m_menu_file->addAction(m_action_opendap);
  m_action_separator_recent = m_menu_file->addSeparator();
  for(int i = 0; i < max_recent_files; ++i)
    m_menu_file->addAction(m_action_recent_file[i]);
  m_menu_file->addSeparator();
  m_menu_file->addAction(m_action_exit);

  m_menu_windows = menuBar()->addMenu(tr("&Window"));
  m_menu_windows->addAction(m_action_tile);
  m_menu_windows->addAction(m_action_close_all);

  m_menu_help = menuBar()->addMenu(tr("&Help"));
  m_menu_help->addAction(m_action_about);

  ///////////////////////////////////////////////////////////////////////////////////////
  //toolbar
  ///////////////////////////////////////////////////////////////////////////////////////

  m_tool_bar = addToolBar(tr("&File"));
  m_tool_bar->addAction(m_action_open);
  m_tool_bar->addAction(m_action_opendap);

  //avoid popup on toolbar
  setContextMenuPolicy(Qt::NoContextMenu);

  ///////////////////////////////////////////////////////////////////////////////////////
  //settings
  ///////////////////////////////////////////////////////////////////////////////////////

  QSettings settings("space", "netcdf_explorer");
  m_sl_recent_files = settings.value("recentFiles").toStringList();
  update_recent_file_actions();

  ///////////////////////////////////////////////////////////////////////////////////////
  //icons
  ///////////////////////////////////////////////////////////////////////////////////////

  m_icon_main = QIcon(":/images/sample.png");
  m_icon_group = QIcon(":/images/folder.png");
  m_icon_dataset = QIcon(":/images/matrix.png");
  m_icon_attribute = QIcon(":/images/document.png");

  ///////////////////////////////////////////////////////////////////////////////////////
  //set main window icon
  ///////////////////////////////////////////////////////////////////////////////////////

  setWindowIcon(m_icon_main);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//MainWindow::about
/////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::about()
{
  QMessageBox::about(this,
    tr("About netCDF Explorer"),
    tr("(c) 2015-2016 Pedro Vicente -- Space Research Software LLC\n\n"));
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//MainWindow::closeEvent
/////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::closeEvent(QCloseEvent *eve)
{
  QSettings settings("space", "netcdf_explorer");
  settings.setValue("recentFiles", m_sl_recent_files);
  eve->accept();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//is_url
/////////////////////////////////////////////////////////////////////////////////////////////////////

bool is_url(QString file_name)
{
  bool isurl = (file_name.left(4) == "http");
  return isurl;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//last_component
/////////////////////////////////////////////////////////////////////////////////////////////////////

QString last_component(const QString &full_file_name)
{
  return QFileInfo(full_file_name).fileName();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//MainWindow::update_recent_file_actions
/////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::update_recent_file_actions()
{
  QMutableStringListIterator i(m_sl_recent_files);
  while(i.hasNext())
  {
    QString file_name = i.next();
    if(is_url(file_name))
    {
      continue;
    }
    if(!QFile::exists(file_name))
    {
      i.remove();
    }
  }

  for(int j = 0; j < max_recent_files; ++j)
  {
    if(j < m_sl_recent_files.count())
    {
      /////////////////////////////////////////////////////////////////////////////////////////////////////
      //display full name of OpenDAP URL or just last component of local file
      /////////////////////////////////////////////////////////////////////////////////////////////////////

      QString file_name;
      if(is_url(m_sl_recent_files[j]))
      {
        file_name = m_sl_recent_files[j];
      }
      else
      {
        file_name = last_component(m_sl_recent_files[j]);
      }
      QString text = tr("&%1 %2")
        .arg(j + 1)
        .arg(file_name);
      m_action_recent_file[j]->setText(text);
      m_action_recent_file[j]->setData(m_sl_recent_files[j]);
      m_action_recent_file[j]->setVisible(true);
    }
    else
    {
      m_action_recent_file[j]->setVisible(false);
    }
  }
  m_action_separator_recent->setVisible(!m_sl_recent_files.isEmpty());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//MainWindow::set_current_file
/////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::set_current_file(const QString &file_name)
{
  m_str_current_file = file_name;

  QString shownName = tr("Untitled");
  if(!m_str_current_file.isEmpty())
  {
    shownName = last_component(m_str_current_file);
    m_sl_recent_files.removeAll(m_str_current_file);
    m_sl_recent_files.prepend(m_str_current_file);
    update_recent_file_actions();
  }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//MainWindow::open_file
/////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::open_file()
{
  QString file_name = QFileDialog::getOpenFileName(this,
    tr("Open File"), ".",
    tr("netCDF Files (*.nc);;All files (*.*)"));

  if(file_name.isEmpty())
    return;

  if(this->read_file(file_name) == NC_NOERR)
  {
    this->set_current_file(file_name);
  }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//MainWindow::open_dap
/////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::open_dap()
{
  QInputDialog dlg(this);
  dlg.setInputMode(QInputDialog::TextInput);
  dlg.setLabelText("DAP url");
  dlg.resize(QSize(400, 60));
  if(QDialog::Accepted == dlg.exec())
  {
    QString file_name = dlg.textValue();
    if(this->read_file(file_name) == NC_NOERR)
    {
      this->set_current_file(file_name);
    }
  }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//MainWindow::open_recent_file
/////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::open_recent_file()
{
  QAction *action = qobject_cast<QAction *>(sender());
  if(action)
  {
    read_file(action->data().toString());
  }
}

///////////////////////////////////////////////////////////////////////////////////////
//MainWindow::read_file
///////////////////////////////////////////////////////////////////////////////////////

int MainWindow::read_file(QString file_name)
{
  QByteArray ba;
  int nc_id;
  std::string path;
  std::string str_file_name;
  QString name;
  int index;
  int len;

  //convert QString to char*
  ba = file_name.toLatin1();

  //convert to std::string
  str_file_name = ba.data();

  if(nc_open(str_file_name.c_str(), NC_NOWRITE, &nc_id) != NC_NOERR)
  {
    qDebug() << str_file_name.c_str();
    return NC2_ERR;
  }

  //group item
  ItemData *item_data_grp = new ItemData(ItemData::Group,
    str_file_name,
    "/",
    "/",
    (ItemData*)NULL,
    (ncdata_t*)NULL);

  //add root
  QTreeWidgetItem *root_item = new QTreeWidgetItem(m_tree);
  index = file_name.lastIndexOf(QChar('/'));
  len = file_name.length();
  name = file_name.right(len - index - 1);
  root_item->setText(0, name);
  root_item->setIcon(0, m_icon_group);
  QVariant data;
  data.setValue(item_data_grp);
  root_item->setData(0, Qt::UserRole, data);

  if(iterate(str_file_name, nc_id, root_item) != NC_NOERR)
  {

  }

  if(nc_close(nc_id) != NC_NOERR)
  {

  }


  return NC_NOERR;
}

///////////////////////////////////////////////////////////////////////////////////////
//MainWindow::iterate
///////////////////////////////////////////////////////////////////////////////////////

int MainWindow::iterate(const std::string& file_name, const int grp_id, QTreeWidgetItem *tree_item_parent)
{
  char grp_nm[NC_MAX_NAME + 1]; // group name 
  char var_nm[NC_MAX_NAME + 1]; // variable name 
  char *grp_nm_fll = NULL; // group full name 
  int nbr_att; // number of attributes 
  int nbr_dmn_grp; // number of dimensions for group 
  int nbr_var; // number of variables 
  int nbr_grp; // number of sub-groups in this group 
  int nbr_dmn_var; // number of dimensions for variable 
  nc_type var_typ; // netCDF type 
  int *grp_ids; // sub-group IDs array
  size_t grp_nm_lng; //lenght of full group name
  int var_dimid[NC_MAX_VAR_DIMS]; // dimensions for variable
  size_t dmn_sz[NC_MAX_VAR_DIMS]; // dimensions for variable sizes
  char dmn_nm_var[NC_MAX_NAME + 1]; //dimension name

  //get item data (of parent item), to store a list of variable names 
  ItemData *item_data_prn = get_item_data(tree_item_parent);
  assert(item_data_prn->m_kind == ItemData::Group || item_data_prn->m_kind == ItemData::Root);

  // get full name of (parent) group
  if(nc_inq_grpname_full(grp_id, &grp_nm_lng, NULL) != NC_NOERR)
  {

  }

  grp_nm_fll = new char[grp_nm_lng + 1];

  if(nc_inq_grpname_full(grp_id, &grp_nm_lng, grp_nm_fll) != NC_NOERR)
  {

  }

  if(nc_inq(grp_id, &nbr_dmn_grp, &nbr_var, &nbr_att, (int *)NULL) != NC_NOERR)
  {

  }

  ///////////////////////////////////////////////////////////////////////////////////////
  //populate group attribues
  ///////////////////////////////////////////////////////////////////////////////////////

  for(int idx_att = 0; idx_att < nbr_att; idx_att++)
  {
    char attr_name[255];
    nc_type attr_typ;
    size_t size_attr;

    if(nc_inq_attname(grp_id, NC_GLOBAL, idx_att, attr_name) != NC_NOERR)
    {

    }

    if(nc_inq_att(grp_id, NC_GLOBAL, attr_name, &attr_typ, &size_attr) != NC_NOERR)
    {

    }

    //store a ncdata_t 
    std::vector<size_t> dim_attr;
    dim_attr.push_back(size_attr);
    ncdata_t *ncattr = new ncdata_t(attr_name, attr_typ, dim_attr);
    dim_attr.clear();

    //set item data
    ItemData *item_data = new ItemData(ItemData::Attribute,
      file_name,
      grp_nm_fll,
      attr_name,
      item_data_prn, //parent for attribute is the goup item data (
      ncattr);

    //store an empty coordinate variable for grid variable compability (print indices in table headers)
    item_data->m_ncvar_crd.push_back(NULL);

    //append item to group item
    QTreeWidgetItem *item_attr = new QTreeWidgetItem(tree_item_parent);
    item_attr->setText(0, attr_name);
    item_attr->setIcon(0, m_icon_attribute);
    QVariant data_attr;
    data_attr.setValue(item_data);
    item_attr->setData(0, Qt::UserRole, data_attr);

  }

  ///////////////////////////////////////////////////////////////////////////////////////
  //populate variables
  ///////////////////////////////////////////////////////////////////////////////////////

  for(int idx_var = 0; idx_var < nbr_var; idx_var++)
  {
    std::vector<size_t> dim; //dimensions for each variable 

    if(nc_inq_var(grp_id, idx_var, var_nm, &var_typ, &nbr_dmn_var, var_dimid, &nbr_att) != NC_NOERR)
    {

    }

    //store variable name in parent group item (for coordinate variables detection)
    item_data_prn->m_var_nms.push_back(var_nm);

    //get dimensions
    for(int idx_dmn = 0; idx_dmn < nbr_dmn_var; idx_dmn++)
    {
      //dimensions belong to groups
      if(nc_inq_dim(grp_id, var_dimid[idx_dmn], dmn_nm_var, &dmn_sz[idx_dmn]) != NC_NOERR)
      {

      }

      //store dimension 
      dim.push_back(dmn_sz[idx_dmn]);
    }

    //store a ncdata_t 
    ncdata_t *ncvar = new ncdata_t(var_nm, var_typ, dim);

    //set item data
    ItemData *item_data_var = new ItemData(ItemData::Variable,
      file_name,
      grp_nm_fll,
      var_nm,
      item_data_prn,
      ncvar);

    //append item
    QTreeWidgetItem *item_var = new QTreeWidgetItem(tree_item_parent);
    item_var->setText(0, var_nm);
    item_var->setIcon(0, m_icon_dataset);
    QVariant data_var;
    data_var.setValue(item_data_var);
    item_var->setData(0, Qt::UserRole, data_var);

    ///////////////////////////////////////////////////////////////////////////////////////
    //populate variable attribues
    ///////////////////////////////////////////////////////////////////////////////////////

    for(int idx_att = 0; idx_att < nbr_att; idx_att++)
    {
      char attr_name[255];
      nc_type attr_typ;
      size_t size_attr;

      if(nc_inq_attname(grp_id, idx_var, idx_att, attr_name) != NC_NOERR)
      {

      }

      if(nc_inq_att(grp_id, idx_var, attr_name, &attr_typ, &size_attr) != NC_NOERR)
      {

      }

      //store a ncdata_t 
      std::vector<size_t> dim_attr;
      dim_attr.push_back(size_attr);
      ncdata_t *ncattr = new ncdata_t(attr_name, attr_typ, dim_attr);
      dim_attr.clear();

      //set item data
      ItemData *item_data = new ItemData(ItemData::Attribute,
        file_name,
        grp_nm_fll,
        attr_name,
        item_data_var, //parent for attribute is the variable item data (contains variable name)
        ncattr);

      //store an empty coordinate variable for grid variable compability (print indices in table headers)
      item_data->m_ncvar_crd.push_back(NULL);

      //append item to variable item
      QTreeWidgetItem *item_attr = new QTreeWidgetItem(item_var);
      item_attr->setText(0, attr_name);
      item_attr->setIcon(0, m_icon_attribute);
      QVariant data_attr;
      data_attr.setValue(item_data);
      item_attr->setData(0, Qt::UserRole, data_attr);
    }
  }

  if(nc_inq_grps(grp_id, &nbr_grp, (int *)NULL) != NC_NOERR)
  {

  }

  grp_ids = new int[nbr_grp];

  if(nc_inq_grps(grp_id, &nbr_grp, grp_ids) != NC_NOERR)
  {

  }

  ///////////////////////////////////////////////////////////////////////////////////////
  //populate groups
  ///////////////////////////////////////////////////////////////////////////////////////

  for(int idx_grp = 0; idx_grp < nbr_grp; idx_grp++)
  {
    if(nc_inq_grpname(grp_ids[idx_grp], grp_nm) != NC_NOERR)
    {

    }

    //group item
    ItemData *item_data_grp = new ItemData(ItemData::Group,
      file_name,
      grp_nm_fll,
      grp_nm,
      item_data_prn,
      (ncdata_t*)NULL);

    //group item
    QTreeWidgetItem *item_grp = new QTreeWidgetItem(tree_item_parent);
    item_grp->setText(0, grp_nm);
    item_grp->setIcon(0, m_icon_group);
    QVariant data;
    data.setValue(item_data_grp);
    item_grp->setData(0, Qt::UserRole, data);

    if(iterate(file_name, grp_ids[idx_grp], item_grp) != NC_NOERR)
    {

    }
  }

  delete[] grp_ids;
  delete[] grp_nm_fll;

  return NC_NOERR;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//TableModel
/////////////////////////////////////////////////////////////////////////////////////////////////////

class TableModel : public QAbstractTableModel
{
public:
  TableModel(QObject *parent, ItemData *item_data);
  int rowCount(const QModelIndex &parent = QModelIndex()) const;
  int columnCount(const QModelIndex &parent = QModelIndex()) const;
  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const;
  //display custom header data
  QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const;

  ChildWindow* m_widget; //get layers in toolbar
  int m_nbr_rows;   // number of rows
  int m_nbr_cols;   // number of columns
  void data_changed(); //update table view when change of layer

  ItemData *m_item_data; // the tree item that generated this grid
  ncdata_t *m_ncdata; // netCDF data to display (convenience pointer to data in ItemData)
  int m_dim_rows;   // choose rows (convenience duplicate to data in ItemData)
  int m_dim_cols;   // choose columns (convenience duplicate to data in ItemData)
  std::vector<ncdata_t *> m_ncvar_crd; // optional coordinate variables for variable (convenience duplicate to data in ItemData)
};

/////////////////////////////////////////////////////////////////////////////////////////////////////
//ChildWindowTable
//model/view
/////////////////////////////////////////////////////////////////////////////////////////////////////

class ChildWindowTable : public ChildWindow
{
public:
  ChildWindowTable(QWidget *parent, ItemData *item_data) :
    ChildWindow(parent, item_data)
  {
    //each new table widget has its own model
    m_model = new TableModel(this, item_data);
    m_model->m_widget = this;
    m_table = new QTableView(this);
    m_table->setModel(m_model);

    //set default row height
    QHeaderView *verticalHeader = m_table->verticalHeader();
#if QT_VERSION >= 0x050000
    verticalHeader->sectionResizeMode(QHeaderView::Fixed);
#else
    verticalHeader->setResizeMode(QHeaderView::Fixed);
#endif
    verticalHeader->setDefaultSectionSize(24);
    setCentralWidget(m_table);
  }
private:
  QTableView *m_table;
};

///////////////////////////////////////////////////////////////////////////////////////
//MainWindow::add_table
///////////////////////////////////////////////////////////////////////////////////////

void MainWindow::add_table(ItemData *item_data)
{
  ChildWindowTable *window = new ChildWindowTable(this, item_data);
  m_mdi_area->addSubWindow(window);
  window->show();
}

///////////////////////////////////////////////////////////////////////////////////////
//ChildWindow::ChildWindow
///////////////////////////////////////////////////////////////////////////////////////

ChildWindow::ChildWindow(QWidget *parent, ItemData *item_data) :
QMainWindow(parent),
m_ncdata(item_data->m_ncdata)
{
  QString str;
  str.sprintf(" : %s", item_data->m_item_nm.c_str());
  this->setWindowTitle(last_component(item_data->m_file_name.c_str()) + str);

  float *buf_float = NULL;
  double *buf_double = NULL;
  int *buf_int = NULL;
  short *buf_short = NULL;
  signed char *buf_byte = NULL;
  unsigned char *buf_ubyte = NULL;
  unsigned short *buf_ushort = NULL;
  unsigned int *buf_uint = NULL;
  long long *buf_int64 = NULL;
  unsigned long long *buf_uint64 = NULL;

  //currently selected layers for dimensions greater than two are the first layer
  if(m_ncdata->m_dim.size() > 2)
  {
    for(size_t idx_dmn = 0; idx_dmn < m_ncdata->m_dim.size() - 2; idx_dmn++)
    {
      m_layer.push_back(0);
    }
  }

  QSignalMapper *signal_mapper_next = NULL;
  QSignalMapper *signal_mapper_previous = NULL;
  QSignalMapper *signal_mapper_combo = NULL;
  //data has layers
  if(item_data->m_ncdata->m_dim.size() > 2)
  {
    m_tool_bar = addToolBar(tr("Layers"));
    signal_mapper_next = new QSignalMapper(this);
    signal_mapper_previous = new QSignalMapper(this);
    signal_mapper_combo = new QSignalMapper(this);
    connect(signal_mapper_next, SIGNAL(mapped(int)), this, SLOT(next_layer(int)));
    connect(signal_mapper_previous, SIGNAL(mapped(int)), this, SLOT(previous_layer(int)));
    connect(signal_mapper_combo, SIGNAL(mapped(int)), this, SLOT(combo_layer(int)));
  }

  //number of dimensions above a two-dimensional dataset
  for(size_t idx_dmn = 0; idx_dmn < m_layer.size(); idx_dmn++)
  {
    ///////////////////////////////////////////////////////////////////////////////////////
    //next layer
    ///////////////////////////////////////////////////////////////////////////////////////

    QAction *action_next  = new QAction(tr("&Next layer..."), this);
    action_next->setIcon(QIcon(":/images/right.png"));
    action_next->setStatusTip(tr("Next layer"));
    connect(action_next, SIGNAL(triggered()), signal_mapper_next, SLOT(map()));
    signal_mapper_next->setMapping(action_next, idx_dmn);

    ///////////////////////////////////////////////////////////////////////////////////////
    //previous layer
    ///////////////////////////////////////////////////////////////////////////////////////

    QAction *action_previous = new QAction(tr("&Previous layer..."), this);
    action_previous->setIcon(QIcon(":/images/left.png"));
    action_previous->setStatusTip(tr("Previous layer"));
    connect(action_previous, SIGNAL(triggered()), signal_mapper_previous, SLOT(map()));
    signal_mapper_previous->setMapping(action_previous, idx_dmn);

    ///////////////////////////////////////////////////////////////////////////////////////
    //add to toolbar
    ///////////////////////////////////////////////////////////////////////////////////////

    m_tool_bar->addAction(action_next);
    m_tool_bar->addAction(action_previous);

    ///////////////////////////////////////////////////////////////////////////////////////
    //add combo box with layers, fill with possible coordinate variables and store combo in vector 
    ///////////////////////////////////////////////////////////////////////////////////////

    QComboBox *combo = new QComboBox;
    QFont font = combo->font();
    font.setPointSize(9);
    combo->setFont(font);
    QStringList list;

    //coordinate variable exists
    if(item_data->m_ncvar_crd[idx_dmn] != NULL)
    {
      void *buf = item_data->m_ncvar_crd[idx_dmn]->m_buf;
      size_t size = item_data->m_ncdata->m_dim[idx_dmn];
      switch(item_data->m_ncvar_crd[idx_dmn]->m_nc_type)
      {
      case NC_FLOAT:
        buf_float = static_cast<float*> (buf);
        for(size_t idx = 0; idx < size; idx++)
        {
          str.sprintf(get_format(NC_FLOAT), buf_float[idx]);
          list.append(str);
        }
        break;
      case NC_DOUBLE:
        buf_double = static_cast<double*> (buf);
        for(size_t idx = 0; idx < size; idx++)
        {
          str.sprintf(get_format(NC_DOUBLE), buf_double[idx]);
          list.append(str);
        }
        break;
      case NC_INT:
        buf_int = static_cast<int*> (buf);
        for(size_t idx = 0; idx < size; idx++)
        {
          str.sprintf(get_format(NC_INT), buf_int[idx]);
          list.append(str);
        }
        break;
      case NC_SHORT:
        buf_short = static_cast<short*> (buf);
        for(size_t idx = 0; idx < size; idx++)
        {
          str.sprintf(get_format(NC_SHORT), buf_short[idx]);
          list.append(str);
        }
        break;
      case NC_BYTE:
        buf_byte = static_cast<signed char*>  (buf);
        for(size_t idx = 0; idx < size; idx++)
        {
          str.sprintf(get_format(NC_BYTE), buf_byte[idx]);
          list.append(str);
        }
        break;
      case NC_UBYTE:
        buf_ubyte = static_cast<unsigned char*> (buf);
        for(size_t idx = 0; idx < size; idx++)
        {
          str.sprintf(get_format(NC_UBYTE), buf_ubyte[idx]);
          list.append(str);
        }
        break;
      case NC_USHORT:
        buf_ushort = static_cast<unsigned short*> (buf);
        for(size_t idx = 0; idx < size; idx++)
        {
          str.sprintf(get_format(NC_USHORT), buf_ushort[idx]);
          list.append(str);
        }
        break;
      case NC_UINT:
        buf_uint = static_cast<unsigned int*> (buf);
        for(size_t idx = 0; idx < size; idx++)
        {
          str.sprintf(get_format(NC_UINT), buf_uint[idx]);
          list.append(str);
        }
        break;
      case NC_INT64:
        buf_int64 = static_cast<long long*> (buf);
        for(size_t idx = 0; idx < size; idx++)
        {
          str.sprintf(get_format(NC_INT64), buf_int64[idx]);
          list.append(str);
        }
        break;
      case NC_UINT64:
        buf_uint64 = static_cast<unsigned long long*> (buf);
        for(size_t idx = 0; idx < size; idx++)
        {
          str.sprintf(get_format(NC_UINT64), buf_uint64[idx]);
          list.append(str);
        }
        break;
      } //switch

    }
    else
    {
      for(unsigned int idx = 0; idx < item_data->m_ncdata->m_dim[idx_dmn]; idx++)
      {
        str.sprintf("%u", idx + 1);
        list.append(str);
      }
    }

    combo->addItems(list);
    connect(combo, SIGNAL(currentIndexChanged(int)), signal_mapper_combo, SLOT(map()));
    signal_mapper_combo->setMapping(combo, idx_dmn);
    m_tool_bar->addWidget(combo);
    m_vec_combo.push_back(combo);
  }
}

///////////////////////////////////////////////////////////////////////////////////////
//ChildWindow::previous_layer
///////////////////////////////////////////////////////////////////////////////////////

void ChildWindow::previous_layer(int idx_layer)
{
  m_layer[idx_layer]--;
  if(m_layer[idx_layer] < 0)
  {
    m_layer[idx_layer] = 0;
    return;
  }
  QComboBox *combo = m_vec_combo.at(idx_layer);
  combo->setCurrentIndex(m_layer[idx_layer]);
  update();

  m_model->data_changed();

}

///////////////////////////////////////////////////////////////////////////////////////
//ChildWindow::next_layer
///////////////////////////////////////////////////////////////////////////////////////

void ChildWindow::next_layer(int idx_layer)
{
  m_layer[idx_layer]++;
  if((size_t)m_layer[idx_layer] >= m_ncdata->m_dim[idx_layer])
  {
    m_layer[idx_layer] = m_ncdata->m_dim[idx_layer] - 1;
    return;
  }
  QComboBox *combo = m_vec_combo.at(idx_layer);
  combo->setCurrentIndex(m_layer[idx_layer]);
  update();

  m_model->data_changed();

}

///////////////////////////////////////////////////////////////////////////////////////
//ChildWindow::combo_layer
///////////////////////////////////////////////////////////////////////////////////////

void ChildWindow::combo_layer(int idx_layer)
{
  QComboBox *combo = m_vec_combo.at(idx_layer);
  m_layer[idx_layer] = combo->currentIndex();;
  update();

  m_model->data_changed();
}

///////////////////////////////////////////////////////////////////////////////////////
//FileTreeWidget::FileTreeWidget 
///////////////////////////////////////////////////////////////////////////////////////

FileTreeWidget::FileTreeWidget(QWidget *parent) : QTreeWidget(parent)
{
  setContextMenuPolicy(Qt::CustomContextMenu);

  //right click menu
  connect(this, SIGNAL(customContextMenuRequested(const QPoint &)), SLOT(show_context_menu(const QPoint &)));

  //double click
  connect(this, SIGNAL(itemDoubleClicked(QTreeWidgetItem*, int)), this, SLOT(add_grid()));
}

///////////////////////////////////////////////////////////////////////////////////////
//FileTreeWidget::~FileTreeWidget
///////////////////////////////////////////////////////////////////////////////////////

FileTreeWidget::~FileTreeWidget()
{
  QTreeWidgetItemIterator it(this);
  while(*it)
  {
    ItemData *item_data = get_item_data(*it);
    delete item_data;
    ++it;
  }

}

///////////////////////////////////////////////////////////////////////////////////////
//FileTreeWidget::enable_data
///////////////////////////////////////////////////////////////////////////////////////

bool FileTreeWidget::enable_data(ItemData *item_data)
{
  switch(item_data->m_ncdata->m_nc_type)
  {
  case NC_FLOAT:
  case NC_DOUBLE:
  case NC_INT:
  case NC_SHORT:
  case NC_CHAR:
  case NC_BYTE:
  case NC_UBYTE:
  case NC_USHORT:
  case NC_UINT:
  case NC_INT64:
  case NC_UINT64:
  case NC_STRING:
    return true;
  };

  return false;
}

///////////////////////////////////////////////////////////////////////////////////////
//FileTreeWidget::show_context_menu
///////////////////////////////////////////////////////////////////////////////////////

void FileTreeWidget::show_context_menu(const QPoint &p)
{
  QTreeWidgetItem *item = static_cast <QTreeWidgetItem*> (itemAt(p));
  ItemData *item_data = get_item_data(item);
  if(item_data->m_kind != ItemData::Variable && item_data->m_kind != ItemData::Attribute)
  {
    return;
  }
  QMenu menu;
  QAction *action_grid = new QAction("Grid...", this);;
  connect(action_grid, SIGNAL(triggered()), this, SLOT(add_grid()));
  if(!enable_data(item_data))
  {
    action_grid->setEnabled(false);
  }
  menu.addAction(action_grid);
  menu.exec(QCursor::pos());
}

///////////////////////////////////////////////////////////////////////////////////////
//FileTreeWidget::add_grid
///////////////////////////////////////////////////////////////////////////////////////

void FileTreeWidget::add_grid()
{
  QTreeWidgetItem *item = static_cast <QTreeWidgetItem*> (currentItem());
  ItemData *item_data = get_item_data(item);
  assert(item_data->m_kind == ItemData::Variable || item_data->m_kind == ItemData::Attribute);
  if(!enable_data(item_data))
  {
    return;
  }
  if(item_data->m_kind == ItemData::Variable)
  {
    this->load_item(item);
    m_main_window->add_table(item_data);
  }
  else if(item_data->m_kind == ItemData::Attribute)
  {
    this->load_item_attribute(item);
    m_main_window->add_table(item_data);
  }

}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//FileTreeWidget::load_item
/////////////////////////////////////////////////////////////////////////////////////////////////////

void FileTreeWidget::load_item(QTreeWidgetItem  *item)
{
  char var_nm[NC_MAX_NAME + 1]; // variable name 
  char dmn_nm_var[NC_MAX_NAME + 1]; //dimension name
  int nc_id;
  int grp_id;
  int var_id;
  nc_type var_type;
  int nbr_dmn;
  int var_dimid[NC_MAX_VAR_DIMS];
  size_t dmn_sz[NC_MAX_VAR_DIMS];
  size_t buf_sz; // variable size
  int fl_fmt;

  ItemData *item_data = get_item_data(item);
  assert(item_data->m_kind == ItemData::Variable);

  //if not loaded, read buffer from file 
  if(item_data->m_ncdata->m_buf != NULL)
  {
    return;
  }

  if(nc_open(item_data->m_file_name.c_str(), NC_NOWRITE, &nc_id) != NC_NOERR)
  {

  }

  //need a file format inquiry, since nc_inq_grp_full_ncid does not handle netCDF3 cases
  if(nc_inq_format(nc_id, &fl_fmt) != NC_NOERR)
  {

  }

  if(fl_fmt == NC_FORMAT_NETCDF4 || fl_fmt == NC_FORMAT_NETCDF4_CLASSIC)
  {
    // obtain group ID for netCDF4 files
    if(nc_inq_grp_full_ncid(nc_id, item_data->m_grp_nm_fll.c_str(), &grp_id) != NC_NOERR)
    {

    }
  }
  else
  {
    //make the group ID the file ID for netCDF3 cases
    grp_id = nc_id;
  }

  //all hunky dory from here 

  // get variable ID
  if(nc_inq_varid(grp_id, item_data->m_item_nm.c_str(), &var_id) != NC_NOERR)
  {

  }

  if(nc_inq_var(grp_id, var_id, var_nm, &var_type, &nbr_dmn, var_dimid, (int *)NULL) != NC_NOERR)
  {

  }

  //get dimensions 
  for(int idx_dmn = 0; idx_dmn < nbr_dmn; idx_dmn++)
  {
    int has_crd_var = 0;

    //dimensions belong to groups
    if(nc_inq_dim(grp_id, var_dimid[idx_dmn], dmn_nm_var, &dmn_sz[idx_dmn]) != NC_NOERR)
    {

    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////
    //look up possible coordinate variables
    //traverse all variables in group and match a variable name 
    /////////////////////////////////////////////////////////////////////////////////////////////////////

    for(size_t idx_var = 0; idx_var < item_data->m_item_data_prn->m_var_nms.size(); idx_var++)
    {
      std::string var_nm(item_data->m_item_data_prn->m_var_nms[idx_var]);

      if(var_nm == std::string(dmn_nm_var))
      {
        has_crd_var = 1;
        break;
      }
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////
    //a coordinate variable was found
    //since the lookup was only in the same group, get the variable information on this group 
    /////////////////////////////////////////////////////////////////////////////////////////////////////

    if(has_crd_var)
    {
      int crd_var_id;
      char crd_var_nm[NC_MAX_NAME + 1];
      int crd_nbr_dmn;
      int crd_var_dimid[NC_MAX_VAR_DIMS];
      size_t crd_dmn_sz[NC_MAX_VAR_DIMS];
      nc_type crd_var_type = NC_NAT;

      // get coordinate variable ID (using the dimension name, since there was a match to a variable)
      if(nc_inq_varid(grp_id, dmn_nm_var, &crd_var_id) != NC_NOERR)
      {

      }

      if(nc_inq_var(grp_id, crd_var_id, crd_var_nm, &crd_var_type, &crd_nbr_dmn, crd_var_dimid, (int *)NULL) != NC_NOERR)
      {

      }

      assert(std::string(crd_var_nm) == std::string(dmn_nm_var));

      if(crd_nbr_dmn == 1)
      {
        //get size
        if(nc_inq_dim(grp_id, crd_var_dimid[0], (char *)NULL, &crd_dmn_sz[0]) != NC_NOERR)
        {

        }

        //store dimension 
        std::vector<size_t> dim; //dimensions for each variable 
        dim.push_back(crd_dmn_sz[0]);

        //store a ncdata_t
        ncdata_t *ncvar = new ncdata_t(crd_var_nm, crd_var_type, dim);

        //allocate, load 
        ncvar->store(load_variable(grp_id, crd_var_id, crd_var_type, crd_dmn_sz[0]));

        //and store in tree 
        item_data->m_ncvar_crd.push_back(ncvar);
      }
    }
    else
    {
      item_data->m_ncvar_crd.push_back(NULL); //no coordinate variable for this dimension
    }
  }

  //define buffer size
  buf_sz = 1;
  for(int idx_dmn = 0; idx_dmn < nbr_dmn; idx_dmn++)
  {
    buf_sz *= dmn_sz[idx_dmn];
  }

  //allocate buffer and store in item data 
  item_data->m_ncdata->store(load_variable(grp_id, var_id, var_type, buf_sz));

  if(nc_close(nc_id) != NC_NOERR)
  {

  }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//FileTreeWidget::load_item_attribute
//A netCDF attribute has a netCDF variable to which it is assigned, a name, a type, a length, and a sequence of one or more values.
/////////////////////////////////////////////////////////////////////////////////////////////////////

void FileTreeWidget::load_item_attribute(QTreeWidgetItem  *item)
{
  int nc_id = -1;
  int grp_id = -1;
  int parent_id = -1;
  size_t attr_sz; // attribute size
  int fl_fmt;

  //get item data of parent item, to detect if parent is group or variable
  ItemData *item_data_prn = get_item_data(item->parent());
  assert(item_data_prn->m_kind == ItemData::Group
    || item_data_prn->m_kind == ItemData::Root
    || item_data_prn->m_kind == ItemData::Variable);

  ItemData *item_data = get_item_data(item);
  assert(item_data->m_kind == ItemData::Attribute);

  //attribute name is tree item name
  const char *attr_nm = item_data->m_item_nm.c_str();
  //attribute type is stored in ncvar_t variable
  nc_type attr_typ = item_data->m_ncdata->m_nc_type;

  //if not loaded, read buffer from file 
  if(item_data->m_ncdata->m_buf != NULL)
  {
    return;
  }

  if(nc_open(item_data->m_file_name.c_str(), NC_NOWRITE, &nc_id) != NC_NOERR)
  {

  }

  //need a file format inquiry, since nc_inq_grp_full_ncid does not handle netCDF3 cases
  if(nc_inq_format(nc_id, &fl_fmt) != NC_NOERR)
  {

  }

  if(fl_fmt == NC_FORMAT_NETCDF4 || fl_fmt == NC_FORMAT_NETCDF4_CLASSIC)
  {
    // obtain group ID for netCDF4 files
    if(nc_inq_grp_full_ncid(nc_id, item_data->m_grp_nm_fll.c_str(), &grp_id) != NC_NOERR)
    {

    }
  }
  else
  {
    //make the group ID the file ID for netCDF3 cases
    grp_id = nc_id;
  }

  if(item_data_prn->m_kind == ItemData::Variable)
  {
    //variable name is the parent item data item name
    const char *var_nm = item_data->m_item_data_prn->m_item_nm.c_str();

    // get variable ID
    if(nc_inq_varid(grp_id, var_nm, &parent_id) != NC_NOERR)
    {

    }
  }
  else if(item_data_prn->m_kind == ItemData::Group || item_data_prn->m_kind == ItemData::Root)
  {
    parent_id = NC_GLOBAL;
  }


  if(nc_inq_attlen(grp_id, parent_id, attr_nm, &attr_sz))
  {

  }

  //allocate buffer and store in item data 
  item_data->m_ncdata->store(load_attribute(grp_id, parent_id, attr_nm, attr_typ, attr_sz));

  if(nc_close(nc_id) != NC_NOERR)
  {

  }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//FileTreeWidget::load_variable
/////////////////////////////////////////////////////////////////////////////////////////////////////

void* FileTreeWidget::load_variable(const int nc_id, const int var_id, const nc_type var_type, size_t buf_sz)
{
  void *buf = NULL;
  switch(var_type)
  {
  case NC_FLOAT:
    buf = malloc(buf_sz * sizeof(float));
    if(nc_get_var_float(nc_id, var_id, static_cast<float *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_DOUBLE:
    buf = malloc(buf_sz * sizeof(double));
    if(nc_get_var_double(nc_id, var_id, static_cast<double *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_INT:
    buf = malloc(buf_sz * sizeof(int));
    if(nc_get_var_int(nc_id, var_id, static_cast<int *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_SHORT:
    buf = malloc(buf_sz * sizeof(short));
    if(nc_get_var_short(nc_id, var_id, static_cast<short *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_CHAR:
    buf = malloc(buf_sz * sizeof(char));
    if(nc_get_var_text(nc_id, var_id, static_cast<char *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_BYTE:
    buf = malloc(buf_sz * sizeof(signed char));
    if(nc_get_var_schar(nc_id, var_id, static_cast<signed char *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_UBYTE:
    buf = malloc(buf_sz * sizeof(unsigned char));
    if(nc_get_var_uchar(nc_id, var_id, static_cast<unsigned char *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_USHORT:
    buf = malloc(buf_sz * sizeof(unsigned short));
    if(nc_get_var_ushort(nc_id, var_id, static_cast<unsigned short *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_UINT:
    buf = malloc(buf_sz * sizeof(unsigned int));
    if(nc_get_var_uint(nc_id, var_id, static_cast<unsigned int *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_INT64:
    buf = malloc(buf_sz * sizeof(long long));
    if(nc_get_var_longlong(nc_id, var_id, static_cast<long long *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_UINT64:
    buf = malloc(buf_sz * sizeof(unsigned long long));
    if(nc_get_var_ulonglong(nc_id, var_id, static_cast<unsigned long long *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_STRING:
    buf = malloc(buf_sz * sizeof(char*));
    if(nc_get_var_string(nc_id, var_id, static_cast<char* *>(buf)) != NC_NOERR)
    {
    }
    break;
  }
  return buf;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//FileTreeWidget::load_attribute
/////////////////////////////////////////////////////////////////////////////////////////////////////

void* FileTreeWidget::load_attribute(const int nc_id, const int var_id, const char *attr_name, const nc_type var_type, size_t buf_sz)
{
  void *buf = NULL;
  switch(var_type)
  {
  case NC_FLOAT:
    buf = malloc(buf_sz * sizeof(float));
    if(nc_get_att_float(nc_id, var_id, attr_name, static_cast<float *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_DOUBLE:
    buf = malloc(buf_sz * sizeof(double));
    if(nc_get_att_double(nc_id, var_id, attr_name, static_cast<double *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_INT:
    buf = malloc(buf_sz * sizeof(int));
    if(nc_get_att_int(nc_id, var_id, attr_name, static_cast<int *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_SHORT:
    buf = malloc(buf_sz * sizeof(short));
    if(nc_get_att_short(nc_id, var_id, attr_name, static_cast<short *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_CHAR:
    buf = malloc(buf_sz * sizeof(char));
    if(nc_get_att_text(nc_id, var_id, attr_name, static_cast<char *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_BYTE:
    buf = malloc(buf_sz * sizeof(signed char));
    if(nc_get_att_schar(nc_id, var_id, attr_name, static_cast<signed char *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_UBYTE:
    buf = malloc(buf_sz * sizeof(unsigned char));
    if(nc_get_att_uchar(nc_id, var_id, attr_name, static_cast<unsigned char *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_USHORT:
    buf = malloc(buf_sz * sizeof(unsigned short));
    if(nc_get_att_ushort(nc_id, var_id, attr_name, static_cast<unsigned short *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_UINT:
    buf = malloc(buf_sz * sizeof(unsigned int));
    if(nc_get_att_uint(nc_id, var_id, attr_name, static_cast<unsigned int *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_INT64:
    buf = malloc(buf_sz * sizeof(long long));
    if(nc_get_att_longlong(nc_id, var_id, attr_name, static_cast<long long *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_UINT64:
    buf = malloc(buf_sz * sizeof(unsigned long long));
    if(nc_get_att_ulonglong(nc_id, var_id, attr_name, static_cast<unsigned long long *>(buf)) != NC_NOERR)
    {
    }
    break;
  case NC_STRING:
    buf = malloc(buf_sz * sizeof(char*));
    if(nc_get_att_string(nc_id, var_id, attr_name, static_cast<char* *>(buf)) != NC_NOERR)
    {
    }
    break;
  }
  return buf;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//get_format
//Provide sprintf() format string for specified netCDF type
//Based on NCO utilities
/////////////////////////////////////////////////////////////////////////////////////////////////////

const char* get_format(const nc_type typ)
{
  switch(typ)
  {
  case NC_FLOAT:
    return "%g";
  case NC_DOUBLE:
    return "%.12g";
  case NC_INT:
    return "%i";
  case NC_SHORT:
    return "%hi";
  case NC_CHAR:
    return "%c";
  case NC_BYTE:
    return "%hhi";
  case NC_UBYTE:
    return "%hhu";
  case NC_USHORT:
    return "%hu";
  case NC_UINT:
    return "%u";
  case NC_INT64:
    return "%lli";
  case NC_UINT64:
    return "%llu";
  case NC_STRING:
    return "%s";
  }
  return NULL;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//TableModel::TableModel
/////////////////////////////////////////////////////////////////////////////////////////////////////

TableModel::TableModel(QObject *parent, ItemData *item_data) :
QAbstractTableModel(parent),
m_widget(NULL),
m_item_data(item_data),
m_ncdata(item_data->m_ncdata),
m_ncvar_crd(item_data->m_ncvar_crd)
{
  /////////////////////////////////////////////////////////////////////////////////////////////////////
  //define grid
  /////////////////////////////////////////////////////////////////////////////////////////////////////

  if(m_ncdata->m_dim.size() == 0)
  {
    m_dim_rows = -1;
    m_dim_cols = -1;
    m_nbr_rows = 1;
    m_nbr_cols = 1;
  }
  else if(m_ncdata->m_dim.size() == 1)
  {
    m_dim_rows = 0;
    m_dim_cols = -1;
    //for NC_CHAR define rank 0, so that all text is displayed in one grid cell
    if(NC_CHAR == m_ncdata->m_nc_type)
    {
      m_nbr_rows = 1;
    }
    else
    {
      m_nbr_rows = m_ncdata->m_dim[m_dim_rows];
    }
    m_nbr_cols = 1;
  }
  else
  {
    m_dim_cols = m_ncdata->m_dim.size() - 1; //2 for 3D
    m_dim_rows = m_ncdata->m_dim.size() - 2; //1 for 3D
    m_nbr_rows = m_ncdata->m_dim[m_dim_rows];
    m_nbr_cols = m_ncdata->m_dim[m_dim_cols];
  }

}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//TableModel::rowCount
/////////////////////////////////////////////////////////////////////////////////////////////////////

int TableModel::rowCount(const QModelIndex &) const
{
  return m_nbr_rows;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//TableModel::columnCount
/////////////////////////////////////////////////////////////////////////////////////////////////////

int TableModel::columnCount(const QModelIndex &) const
{
  return m_nbr_cols;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//TableModel::data_changed
//update table view when change of layer
/////////////////////////////////////////////////////////////////////////////////////////////////////

void TableModel::data_changed()
{
  QModelIndex top = index(0, 0, QModelIndex());
  QModelIndex bottom = index(m_nbr_rows, m_nbr_cols, QModelIndex());
  dataChanged(top, bottom);
}


/////////////////////////////////////////////////////////////////////////////////////////////////////
//TableModel::headerData
//For horizontal headers, the section number corresponds to the column number.
//Similarly, for vertical headers, the section number corresponds to the row number.
/////////////////////////////////////////////////////////////////////////////////////////////////////

QVariant TableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
  QString str;

  if(role != Qt::DisplayRole)
  {
    return QVariant();
  }

  /////////////////////////////////////////////////////////////////////////////////////////////////////
  //labels for columns
  /////////////////////////////////////////////////////////////////////////////////////////////////////

  if(orientation == Qt::Horizontal)
  {
    //columns not defined
    if(m_dim_cols == -1)
    {
      assert(m_nbr_cols == 1);
      str.sprintf("%d", 1);
      return str;
    }
    else
    {
      //coordinate variable exists
      int idx_col = section;
      if(m_ncvar_crd[m_dim_cols] != NULL)
      {
        if(m_ncvar_crd[m_dim_cols]->m_nc_type == NC_FLOAT)
        {
          float *buf_ = static_cast<float*> (m_ncvar_crd[m_dim_cols]->m_buf);
          str.sprintf(get_format(NC_FLOAT), buf_[idx_col]);
          return str;
        }
        else if(m_ncvar_crd[m_dim_cols]->m_nc_type == NC_DOUBLE)
        {
          double *buf_ = static_cast<double*> (m_ncvar_crd[m_dim_cols]->m_buf);
          str.sprintf(get_format(NC_DOUBLE), buf_[idx_col]);
          return str;
        }
        else if(m_ncvar_crd[m_dim_cols]->m_nc_type == NC_INT)
        {
          int *buf_ = static_cast<int*> (m_ncvar_crd[m_dim_cols]->m_buf);
          str.sprintf(get_format(NC_INT), buf_[idx_col]);
          return str;
        }
        else if(m_ncvar_crd[m_dim_cols]->m_nc_type == NC_SHORT)
        {
          short *buf_ = static_cast<short*> (m_ncvar_crd[m_dim_cols]->m_buf);
          str.sprintf(get_format(NC_SHORT), buf_[idx_col]);
          return str;
        }
        else if(m_ncvar_crd[m_dim_cols]->m_nc_type == NC_CHAR)
        {
          char *buf_ = static_cast<char*> (m_ncvar_crd[m_dim_cols]->m_buf);
          str.sprintf(get_format(NC_CHAR), buf_[idx_col]);
          return str;
        }
        else if(m_ncvar_crd[m_dim_cols]->m_nc_type == NC_BYTE)
        {
          signed char *buf_ = static_cast<signed char*> (m_ncvar_crd[m_dim_cols]->m_buf);
          str.sprintf(get_format(NC_BYTE), buf_[idx_col]);
          return str;
        }
        else if(m_ncvar_crd[m_dim_cols]->m_nc_type == NC_UBYTE)
        {
          unsigned char *buf_ = static_cast<unsigned char*> (m_ncvar_crd[m_dim_cols]->m_buf);
          str.sprintf(get_format(NC_UBYTE), buf_[idx_col]);
          return str;
        }
        else if(m_ncvar_crd[m_dim_cols]->m_nc_type == NC_USHORT)
        {
          unsigned short *buf_ = static_cast<unsigned short*> (m_ncvar_crd[m_dim_cols]->m_buf);
          str.sprintf(get_format(NC_USHORT), buf_[idx_col]);
          return str;
        }
        else if(m_ncvar_crd[m_dim_cols]->m_nc_type == NC_UINT)
        {
          unsigned int *buf_ = static_cast<unsigned int*> (m_ncvar_crd[m_dim_cols]->m_buf);
          str.sprintf(get_format(NC_UINT), buf_[idx_col]);
          return str;
        }
        else if(m_ncvar_crd[m_dim_cols]->m_nc_type == NC_INT64)
        {
          long long *buf_ = static_cast<long long*> (m_ncvar_crd[m_dim_cols]->m_buf);
          str.sprintf(get_format(NC_INT64), buf_[idx_col]);
          return str;
        }
        else if(m_ncvar_crd[m_dim_cols]->m_nc_type == NC_UINT64)
        {
          unsigned long long *buf_ = static_cast<unsigned long long*> (m_ncvar_crd[m_dim_cols]->m_buf);
          str.sprintf(get_format(NC_UINT64), buf_[idx_col]);
          return str;
        }
        else if(m_ncvar_crd[m_dim_cols]->m_nc_type == NC_STRING)
        {
          char* *buf_ = static_cast<char**> (m_ncvar_crd[m_dim_cols]->m_buf);
          str.sprintf(get_format(NC_STRING), buf_[idx_col]);
          return str;
        }
      }
      //coordinate variable does not exist: print index
      else
      {
        str.sprintf("%d", idx_col + 1);
        return str;
      }
    }
  }

  /////////////////////////////////////////////////////////////////////////////////////////////////////
  //labels for rows
  /////////////////////////////////////////////////////////////////////////////////////////////////////

  else if(orientation == Qt::Vertical)
  {
    //rows not defined
    if(m_dim_rows == -1)
    {
      str.sprintf("%d", 1);
      return str;
    }
    else
    {
      //coordinate variable exists
      int idx_row = section;
      if(m_ncvar_crd[m_dim_rows] != NULL)
      {
        if(m_ncvar_crd[m_dim_rows]->m_nc_type == NC_FLOAT)
        {
          float *buf_ = static_cast<float*> (m_ncvar_crd[m_dim_rows]->m_buf);
          str.sprintf(get_format(NC_FLOAT), buf_[idx_row]);
          return str;
        }
        else if(m_ncvar_crd[m_dim_rows]->m_nc_type == NC_DOUBLE)
        {
          double *buf_ = static_cast<double*> (m_ncvar_crd[m_dim_rows]->m_buf);
          str.sprintf(get_format(NC_DOUBLE), buf_[idx_row]);
          return str;
        }
        else if(m_ncvar_crd[m_dim_rows]->m_nc_type == NC_INT)
        {
          int *buf_ = static_cast<int*> (m_ncvar_crd[m_dim_rows]->m_buf);
          str.sprintf(get_format(NC_INT), buf_[idx_row]);
          return str;
        }
        else if(m_ncvar_crd[m_dim_rows]->m_nc_type == NC_SHORT)
        {
          short *buf_ = static_cast<short*> (m_ncvar_crd[m_dim_rows]->m_buf);
          str.sprintf(get_format(NC_SHORT), buf_[idx_row]);
          return str;
        }
        else if(m_ncvar_crd[m_dim_rows]->m_nc_type == NC_CHAR)
        {
          char *buf_ = static_cast<char*> (m_ncvar_crd[m_dim_rows]->m_buf);
          str.sprintf(get_format(NC_CHAR), buf_[idx_row]);
          return str;
        }
        else if(m_ncvar_crd[m_dim_rows]->m_nc_type == NC_BYTE)
        {
          signed char *buf_ = static_cast<signed char*> (m_ncvar_crd[m_dim_rows]->m_buf);
          str.sprintf(get_format(NC_BYTE), buf_[idx_row]);
          return str;
        }
        else if(m_ncvar_crd[m_dim_rows]->m_nc_type == NC_UBYTE)
        {
          unsigned char *buf_ = static_cast<unsigned char*> (m_ncvar_crd[m_dim_rows]->m_buf);
          str.sprintf(get_format(NC_UBYTE), buf_[idx_row]);
          return str;
        }
        else if(m_ncvar_crd[m_dim_rows]->m_nc_type == NC_USHORT)
        {
          unsigned short *buf_ = static_cast<unsigned short*> (m_ncvar_crd[m_dim_rows]->m_buf);
          str.sprintf(get_format(NC_USHORT), buf_[idx_row]);
          return str;
        }
        else if(m_ncvar_crd[m_dim_rows]->m_nc_type == NC_UINT)
        {
          unsigned int *buf_ = static_cast<unsigned int*> (m_ncvar_crd[m_dim_rows]->m_buf);
          str.sprintf(get_format(NC_UINT), buf_[idx_row]);
          return str;
        }
        else if(m_ncvar_crd[m_dim_rows]->m_nc_type == NC_INT64)
        {
          long long *buf_ = static_cast<long long*> (m_ncvar_crd[m_dim_rows]->m_buf);
          str.sprintf(get_format(NC_INT64), buf_[idx_row]);
          return str;
        }
        else if(m_ncvar_crd[m_dim_rows]->m_nc_type == NC_UINT64)
        {
          unsigned long long *buf_ = static_cast<unsigned long long*> (m_ncvar_crd[m_dim_rows]->m_buf);
          str.sprintf(get_format(NC_UINT64), buf_[idx_row]);
          return str;
        }
        else if(m_ncvar_crd[m_dim_rows]->m_nc_type == NC_STRING)
        {
          char* *buf_ = static_cast<char**> (m_ncvar_crd[m_dim_rows]->m_buf);
          str.sprintf(get_format(NC_STRING), buf_[idx_row]);
          return str;
        }
      }
      //coordinate variable does not exist: print index
      else
      {
        str.sprintf("%d", idx_row + 1);
        return str;
      }
    }
  }

  assert(0);
  return QVariant();

}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//TableModel::data
/////////////////////////////////////////////////////////////////////////////////////////////////////

QVariant TableModel::data(const QModelIndex &index, int role) const
{
  ChildWindow* parent = m_widget;
  QString str;
  size_t idx_buf = 0;
  //3D
  if(parent->m_layer.size() == 1)
  {
    idx_buf = parent->m_layer[0] * m_nbr_rows * m_nbr_cols;
  }
  //4D
  else if(parent->m_layer.size() == 2)
  {
    idx_buf = parent->m_layer[0] * m_ncdata->m_dim[1] + parent->m_layer[1];
    idx_buf *= m_nbr_rows * m_nbr_cols;
  }
  //5D
  else if(parent->m_layer.size() == 3)
  {
    idx_buf = parent->m_layer[0] * m_ncdata->m_dim[1] * m_ncdata->m_dim[2]
      + parent->m_layer[1] * m_ncdata->m_dim[2]
      + parent->m_layer[2];
    idx_buf *= m_nbr_rows * m_nbr_cols;
  }

  //into current index
  idx_buf += index.row() * m_nbr_cols + index.column();

  if(role != Qt::DisplayRole)
  {
    return QVariant();
  }

  /////////////////////////////////////////////////////////////////////////////////////////////////////
  //grid
  /////////////////////////////////////////////////////////////////////////////////////////////////////

  if(m_ncdata->m_nc_type == NC_FLOAT)
  {
    float *buf_ = static_cast<float*> (m_ncdata->m_buf);
    str.sprintf(get_format(NC_FLOAT), buf_[idx_buf]);
    return str;
  }
  else if(m_ncdata->m_nc_type == NC_DOUBLE)
  {
    double *buf_ = static_cast<double*> (m_ncdata->m_buf);
    str.sprintf(get_format(NC_DOUBLE), buf_[idx_buf]);
    return str;
  }
  else if(m_ncdata->m_nc_type == NC_INT)
  {
    int* buf_ = static_cast<int*> (m_ncdata->m_buf);
    str.sprintf(get_format(NC_INT), buf_[idx_buf]);
    return str;
  }
  else if(m_ncdata->m_nc_type == NC_SHORT)
  {
    short *buf_ = static_cast<short*> (m_ncdata->m_buf);
    str.sprintf(get_format(NC_SHORT), buf_[idx_buf]);
    return str;
  }
  else if(m_ncdata->m_nc_type == NC_CHAR)
  {
    char *buf_ = static_cast<char*> (m_ncdata->m_buf);
    //size of string, display in one cell
    for(size_t idx = 0; idx < m_ncdata->m_dim[0]; idx++)
    {
      str += buf_[idx];
    }
    return str;
  }
  else if(m_ncdata->m_nc_type == NC_BYTE)
  {
    signed char *buf_ = static_cast<signed char*> (m_ncdata->m_buf);
    str.sprintf(get_format(NC_BYTE), buf_[idx_buf]);
    return str;
  }
  else if(m_ncdata->m_nc_type == NC_UBYTE)
  {
    unsigned char *buf_ = static_cast<unsigned char*> (m_ncdata->m_buf);
    str.sprintf(get_format(NC_UBYTE), buf_[idx_buf]);
    return str;
  }
  else if(m_ncdata->m_nc_type == NC_USHORT)
  {
    unsigned short *buf_ = static_cast<unsigned short*> (m_ncdata->m_buf);
    str.sprintf(get_format(NC_USHORT), buf_[idx_buf]);
    return str;
  }
  else if(m_ncdata->m_nc_type == NC_UINT)
  {
    unsigned int* buf_ = static_cast<unsigned int*> (m_ncdata->m_buf);
    str.sprintf(get_format(NC_UINT), buf_[idx_buf]);
    return str;
  }
  else if(m_ncdata->m_nc_type == NC_INT64)
  {
    unsigned long long* buf_ = static_cast<unsigned long long*> (m_ncdata->m_buf);
    str.sprintf(get_format(NC_INT64), buf_[idx_buf]);
    return str;
  }
  else if(m_ncdata->m_nc_type == NC_UINT64)
  {
    unsigned long long* buf_ = static_cast<unsigned long long*> (m_ncdata->m_buf);
    str.sprintf(get_format(NC_UINT64), buf_[idx_buf]);
    return str;
  }
  else if(m_ncdata->m_nc_type == NC_STRING)
  {
    char* *buf_ = static_cast<char**> (m_ncdata->m_buf);
    str.sprintf(get_format(NC_STRING), buf_[idx_buf]);
    return str;
  }

  assert(0);
  return QVariant();
}

