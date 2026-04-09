/*
  Single-file: read .dat, build 2-node-per-location directed assignment model,
  reroute land-crossing arcs via waypoint shortest paths (island.bin),
  solve with lazy subtour elimination.

  Build (macOS):
    clang main.c \
      -I/Library/gurobi1300/macos_universal2/include \
      -L/Library/gurobi1300/macos_universal2/lib -lgurobi130 \
      -Wl,-rpath,/Library/gurobi1300/macos_universal2/lib \
      -lm \
      -o demo

  Run:
    ./demo data/data2023spring.dat 1 [timelimit_seconds]


clang main.c \                                    
  -I/Library/gurobi1300/macos_universal2/include \
  -L/Library/gurobi1300/macos_universal2/lib \
  -lgurobi130 \
  -Wl,-rpath,/Library/gurobi1300/macos_universal2/lib \
  -o demo

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <sqlite3.h>

#include "gurobi_c.h"

extern int DistanceLink(double *DistrMtrx, int *FsbleMtrx, int *Type,
                        double *LatLon[4], double *StartEnd,
                        int Size, int SelectedSize);

/* ---------- Constants ---------- */
enum { tSHIP=1, tSTAT=2, tWAYP=3, tENDP=4, tPORT=5 };

#define MAXLON   -4
#define MAXLAT   70
#define MINLAT   60
#define MINLON  -32
#ifndef PI
#define PI 3.14159265358979323846
#endif

/* ---------- Utility helpers ---------- */
static void die(const char *msg) { fprintf(stderr, "%s\n", msg); exit(1); }
static void *xmalloc(size_t n) { void *p = malloc(n); if (!p) die("OOM"); return p; }
static void *xcalloc(size_t n, size_t s) { void *p = calloc(n,s); if(!p) die("OOM"); return p; }
static char *xstrdup(const char *s){ size_t n=strlen(s); char *p=(char*)xmalloc(n+1); memcpy(p,s,n+1); return p; }

/* tokenization like regex: r'"[^"]*"|\S+' (keeps quotes) */
static int tokenize_line(const char *line, char ***tokens_out) {
  int cap=16, cnt=0;
  char **tok=(char**)xmalloc((size_t)cap*sizeof(char*));
  const char *p=line;
  while (*p) {
    while (*p && isspace((unsigned char)*p)) p++;
    if (!*p) break;
    const char *start=p, *end=NULL;
    if (*p=='"') {
      p++;
      while (*p && *p!='"') p++;
      if (*p=='"') p++;
      end=p;
    } else {
      while (*p && !isspace((unsigned char)*p)) p++;
      end=p;
    }
    size_t len=(size_t)(end-start);
    char *t=(char*)xmalloc(len+1);
    memcpy(t,start,len); t[len]='\0';
    if (cnt==cap) { cap*=2; tok=(char**)realloc(tok,(size_t)cap*sizeof(char*)); if(!tok) die("OOM"); }
    tok[cnt++]=t;
  }
  *tokens_out=tok;
  return cnt;
}
static void free_tokens(char **tok, int cnt){ for(int i=0;i<cnt;i++) free(tok[i]); free(tok); }

/* Python degmin2rad (for file coords) */
static double degmin2rad(double degmin_in) {
  double degmin = degmin_in;
  if (fabs(degmin) < 10000.0) degmin *= 100.0;
  double m = (degmin/100.0) - floor(degmin/10000.0)*100.0;
  double deg = (degmin + (200.0/3.0)*m)/10000.0;
  return deg * PI / 180.0;
}

static int write_plot_bundle(const char *fname,
                                int Size,
                                int SelectedSize,
                                const int *nodeTour,  /* length 2*Size */
                                int nodeTourLen,
                                const int *pairDir,   /* length Size: +1 if 2i->2i+1 else -1 */
                                const int *Type,
                                const double *Amount,
                                const double *LatLonRad,
                                const double *FullDist, /* [FullM*FullM] or NULL */
                                const int *FullFsb,     /* [FullM*FullM] or NULL */
                                int FullM)
{
  FILE *fp = fopen(fname, "w");
  if (!fp) { perror("fopen(write_plot_bundle)"); return 1; }

  fprintf(fp, "%s\n", (FullDist && FullFsb && FullM > 0) ? "PLOT_BUNDLE_V3" : "PLOT_BUNDLE_V2");
  fprintf(fp, "Size %d\n", Size);
  fprintf(fp, "SelectedSize %d\n", SelectedSize);

  fprintf(fp, "NodeTour %d", nodeTourLen);
  for (int i = 0; i < nodeTourLen; i++) fprintf(fp, " %d", nodeTour[i]);
  fprintf(fp, "\n");

  fprintf(fp, "PairDir");
  for (int i = 0; i < Size; i++) fprintf(fp, " %d", pairDir[i]); /* +1 or -1 */
  fprintf(fp, "\n");

  fprintf(fp, "Type");
  for (int i = 0; i < SelectedSize; i++) fprintf(fp, " %d", Type[i]);
  fprintf(fp, "\n");

  fprintf(fp, "Amount");
  for (int i = 0; i < SelectedSize; i++) fprintf(fp, " %.17g", Amount[i]);
  fprintf(fp, "\n");

  fprintf(fp, "LatLonRad\n");
  for (int i = 0; i < SelectedSize; i++) {
    fprintf(fp, "%.17g %.17g %.17g %.17g\n",
            LatLonRad[i*4+0], LatLonRad[i*4+1], LatLonRad[i*4+2], LatLonRad[i*4+3]);
  }

  if (FullDist && FullFsb && FullM > 0) {
    fprintf(fp, "FullMatrixSize %d\n", FullM);
    fprintf(fp, "DistMtrx\n");
    for (int i = 0; i < FullM; i++) {
      for (int j = 0; j < FullM; j++) {
        fprintf(fp, "%.17g%s", FullDist[i*FullM + j], (j + 1 == FullM) ? "" : " ");
      }
      fputc('\n', fp);
    }
    fprintf(fp, "FsbleMtrx\n");
    for (int i = 0; i < FullM; i++) {
      for (int j = 0; j < FullM; j++) {
        fprintf(fp, "%d%s", FullFsb[i*FullM + j], (j + 1 == FullM) ? "" : " ");
      }
      fputc('\n', fp);
    }
  }

  fclose(fp);
  return 0;
}

/* From utils.c: degmin2deg used by crossesland() */
static double degmin2deg(double degmin) {
  double min;
  if (fabs(degmin) < 10000) degmin = degmin*100;
  min = (degmin/100) - floor(degmin/10000.)*100.;
  return ((degmin + (200.0/3.0)*min)/10000.);
}

static void deg2point(double *x, double *y, double *Lat, double *Lon, int length, int Norm) {
  double lat65, x65, M, M65, Diff, lat, scale, lon;
  int i;

  lat65 = 65.*PI/180.;
  x65 = (111415.13*cos(lat65)-94.55*cos(3*lat65)+0.12*cos(5*lat65))/60;
  M65 = 7915.704456*log10(tan(PI/4+lat65/2))
        - sin(lat65)*(23.110771+0.052051*(sin(lat65))*sin(lat65));

  lat = MAXLAT*PI/180;
  M = 7915.704456*log10(tan(PI/4+lat/2))-sin(lat)*(23.110771+0.052051*(sin(lat))*sin(lat65));
  Diff = M-M65;
  lat = MINLAT*PI/180;
  M = 7915.704456*log10(tan(PI/4+lat/2))-sin(lat)*(23.110771+0.052051*(sin(lat))*sin(lat65));
  Diff = Diff + M65 - M;
  scale = (double)Norm/(Diff*x65);
  x65 = scale*x65;

  for (i=0; i<length; i++) {
    lon = Lon[i];
    lat = Lat[i]*PI/180;
    M = 7915.704456*log10(tan(PI/4+lat/2))-sin(lat)*(23.110771+0.052051*(sin(lat))*sin(lat65));
    Diff = M65-M;
    y[i] = (int)(Diff*x65);
    x[i] = (int)((lon+18)*x65*60);
  }
}

static int intersects(double s0x[2], double s0y[2], double s1x[2], double s1y[2]) {
  double dx0 = s0x[1]-s0x[0];
  double dx1 = s1x[1]-s1x[0];
  double dy0 = s0y[1]-s0y[0];
  double dy1 = s1y[1]-s1y[0];
  double p0 = dy1*(s1x[1]-s0x[0]) - dx1*(s1y[1]-s0y[0]);
  double p1 = dy1*(s1x[1]-s0x[1]) - dx1*(s1y[1]-s0y[1]);
  double p2 = dy0*(s0x[1]-s1x[0]) - dx0*(s0y[1]-s1y[0]);
  double p3 = dy0*(s0x[1]-s1x[1]) - dx0*(s0y[1]-s1y[1]);
  return ((p0*p1<=0) && (p2*p3<=0));
}

/* LandDeg is (lat array then lon array) in the island.bin format from Python drawTour:
   landata = [lat0..latN-1, lon0..lonN-1]
*/
static int crossesland(double stAx_degmin, double stAy_degmin,
                       double stBx_degmin, double stBy_degmin,
                       const double *LandDeg, int n)
{
  int i, dn = 4;
  double s0x[2], s0y[2], s1x[2], s1y[2];

  s0x[0] = degmin2deg(stAx_degmin);
  s0x[1] = degmin2deg(stBx_degmin);
  s0y[0] = -degmin2deg(stAy_degmin);
  s0y[1] = -degmin2deg(stBy_degmin);
  deg2point(s0x, s0y, s0x, s0y, 2, 1000000);

  for (i = 0; i < n-dn; i += dn) {
    s1x[0] = LandDeg[i];
    s1x[1] = LandDeg[i+dn];
    s1y[0] = LandDeg[i+n];
    s1y[1] = LandDeg[i+dn+n];
    deg2point(s1x, s1y, s1x, s1y, 2, 1000000);
    if (1 == intersects(s0x,s0y,s1x,s1y)) return 0;
  }
  return 1;
}

/* great-circle distance in nautical miles-ish (same r as your python arcdist) */
static double arcdist(double lat1, double lon1, double lat2, double lon2) {
  const double r = 3437.905;
  double angle = sin(lat1)*sin(lat2) + cos(lat1)*cos(lat2)*cos(lon1-lon2);
  if (angle > 1.0) angle = 1.0;
  if (angle < -1.0) angle = -1.0;
  return r * acos(angle);
}

static int minDistance(const double *dist, const int *sptSet, int n) {
  double min = 100000000.0;
  int min_index = 0;
  for (int v = 0; v < n; v++) {
    if (sptSet[v] == 0 && dist[v] <= min) {
      min = dist[v];
      min_index = v;
    }
  }
  return min_index;
}

static double dijkstra_dist(const double *graph, int n, int n_wayp, int src, int dest) {
  const double INFTY = 10000000000.0;
  double *dist = (double*)xmalloc((size_t)n * sizeof(double));
  int *sptSet = (int*)xcalloc((size_t)n, sizeof(int));

  for (int i = 0; i < n_wayp; i++) dist[i] = 2 * INFTY;
  for (int i = n_wayp; i < n; i++) dist[i] = INFTY;
  dist[src] = 0.0;

  for (int count = n_wayp; count < n - 1; count++) {
    int u = minDistance(dist, sptSet, n);
    sptSet[u] = 1;

    int v = dest;
    if (sptSet[v] == 0 && graph[u*n + v] > 0.0 &&
        dist[u] < INFTY && dist[u] + graph[u*n + v] < dist[v]) {
      dist[v] = dist[u] + graph[u*n + v];
    }

    for (v = n_wayp; v < n; v++) {
      if (sptSet[v] == 0 && graph[u*n + v] > 0.0 &&
          dist[u] < INFTY && dist[u] + graph[u*n + v] < dist[v]) {
        dist[v] = dist[u] + graph[u*n + v];
      }
    }
  }

  double d = dist[dest];
  free(dist);
  free(sptSet);
  return d;
}

/* ---------- Data from .dat (like Python readDat) ---------- */
typedef struct {
  int Type;
  int Fixed;
  int Rotated;
  double LatLonRad[4];  /* radians */
  double LatLonDegMin[4]; /* original degmin numbers (needed for crossesland) */
  char *Name;
  char *RawLine;
  char *Comment;
  int Reitur;
  int Tog;
  int PortSelected;
  double BoatData[11];
  int BoatDataLen;
  double Amount;
  double ExtraTime;
} Item;

typedef struct {
  Item *a;
  int n;
  int cap;
} ItemVec;

static void vec_init(ItemVec *v){ v->n=0; v->cap=64; v->a=(Item*)xmalloc((size_t)v->cap*sizeof(Item)); }
static void vec_push(ItemVec *v, Item it){
  if(v->n==v->cap){ v->cap*=2; v->a=(Item*)realloc(v->a,(size_t)v->cap*sizeof(Item)); if(!v->a) die("OOM"); }
  v->a[v->n++]=it;
}
static void vec_free(ItemVec *v){
  for(int i=0;i<v->n;i++){
    free(v->a[i].Name);
    free(v->a[i].RawLine);
    free(v->a[i].Comment);
  }
  free(v->a);
}

/* Read like Python readDat, but also keep original degmin coords for land test */
static void readDat_C(const char *fname, const char *ship_name_plain, ItemVec *out_items,
                      double *out_shipCap, int skip_ports)
{
  char ship_token[256];
  snprintf(ship_token, sizeof(ship_token), "\"%s\"", ship_name_plain);

  FILE *fp = fopen(fname, "rb");
  if (!fp) { perror("fopen"); exit(1); }

  int found_ship = 0;
  double ShipCap = 0.0;
  int tag = 0; /* 0 ALL, 1 WAYPONLY */

  char line[4096];
  while (fgets(line, sizeof(line), fp)) {
    char **tok=NULL; int nt=tokenize_line(line,&tok);
    if (nt <= 1) { free_tokens(tok,nt); break; }

    const char *se="IGNORE";
    if (tag==0) se = tok[0];
    else if (tag==1 && strcmp(tok[0],"WAYP")==0) se = tok[0];

    if (strcmp(se,"BOAT")==0) {
      if (found_ship) {
        tag = 1;
      } else if (nt >= 13 && strcmp(tok[12], ship_token)==0) {
        double data[11];
        for(int i=0;i<11;i++) data[i]=atof(tok[1+i]);
        found_ship = 1;
        ShipCap = data[4];

        Item it; memset(&it,0,sizeof(it));
        it.Type = tSHIP;
        it.Name = xstrdup(ship_token);
        it.Amount = 0; it.ExtraTime = 0;
        it.RawLine = xstrdup(line);
        char *hash = strchr(line, '#');
        if (hash) {
          it.Comment = xstrdup(hash);
          size_t clen = strlen(it.Comment);
          while (clen > 0 && (it.Comment[clen-1] == '\n' || it.Comment[clen-1] == '\r')) {
            it.Comment[--clen] = '\0';
          }
        }
        it.BoatDataLen = 11;
        for (int i = 0; i < 11; i++) it.BoatData[i] = data[i];
        /* store degmin */
        it.LatLonDegMin[0]=data[0]; it.LatLonDegMin[1]=data[1];
        it.LatLonDegMin[2]=data[2]; it.LatLonDegMin[3]=data[3];
        /* radians */
        for(int k=0;k<4;k++) it.LatLonRad[k]=degmin2rad(it.LatLonDegMin[k]);
        vec_push(out_items,it);
      }
    }
    else if (strcmp(se,"STAT")==0 && found_ship) {
      if (nt >= 10) {
        int datai[9];
        for(int i=0;i<9;i++) datai[i]=atoi(tok[1+i]);
        if (datai[2] != 5) {
          Item it; memset(&it,0,sizeof(it));
          it.Type = tSTAT;
          it.Fixed = (datai[2]==2);
          it.Rotated = (datai[2]==1);
          it.Amount = (double)datai[7];
          it.ExtraTime = (double)datai[8];
          it.Reitur = abs(datai[0]);
          it.Tog = abs(datai[1]);
          it.RawLine = xstrdup(line);
          char *hash = strchr(line, '#');
          if (hash) {
            it.Comment = xstrdup(hash);
            size_t clen = strlen(it.Comment);
            while (clen > 0 && (it.Comment[clen-1] == '\n' || it.Comment[clen-1] == '\r')) {
              it.Comment[--clen] = '\0';
            }
          }
          char nm[64];
          snprintf(nm,sizeof(nm),"%d %d",abs(datai[0]),abs(datai[1]));
          it.Name = xstrdup(nm);
          it.LatLonDegMin[0]= (double)datai[3];
          it.LatLonDegMin[1]= (double)datai[4];
          it.LatLonDegMin[2]= (double)datai[5];
          it.LatLonDegMin[3]= (double)datai[6];
          for(int k=0;k<4;k++) it.LatLonRad[k]=degmin2rad(it.LatLonDegMin[k]);
          vec_push(out_items,it);
        }
      }
    }
    else if (strcmp(se,"PORT")==0 && found_ship) {
      if (nt >= 5) {
        Item it; memset(&it,0,sizeof(it));
        it.Type = tPORT;
        it.Name = xstrdup(tok[3]);
        it.Amount = 0; it.ExtraTime = 0;
        it.PortSelected = atoi(tok[4]);
        if (skip_ports) it.PortSelected = 0;
        it.RawLine = xstrdup(line);
        char *hash = strchr(line, '#');
        if (hash) {
          it.Comment = xstrdup(hash);
          size_t clen = strlen(it.Comment);
          while (clen > 0 && (it.Comment[clen-1] == '\n' || it.Comment[clen-1] == '\r')) {
            it.Comment[--clen] = '\0';
          }
        }
        it.LatLonDegMin[0]=atof(tok[1]);
        it.LatLonDegMin[1]=atof(tok[2]);
        it.LatLonDegMin[2]=it.LatLonDegMin[0];
        it.LatLonDegMin[3]=it.LatLonDegMin[1];
        for(int k=0;k<4;k++) it.LatLonRad[k]=degmin2rad(it.LatLonDegMin[k]);
        vec_push(out_items,it);
      }
    }
    else if (strcmp(se,"WAYP")==0) {
      if (nt >= 4 && strcmp(tok[3],"-1")!=0) {
        Item it; memset(&it,0,sizeof(it));
        it.Type = tWAYP;
        it.Name = xstrdup("Wayp");
        it.Amount = 0; it.ExtraTime = 0;
        it.RawLine = xstrdup(line);
        char *hash = strchr(line, '#');
        if (hash) {
          it.Comment = xstrdup(hash);
          size_t clen = strlen(it.Comment);
          while (clen > 0 && (it.Comment[clen-1] == '\n' || it.Comment[clen-1] == '\r')) {
            it.Comment[--clen] = '\0';
          }
        }
        it.LatLonDegMin[0]=atof(tok[1]);
        it.LatLonDegMin[1]=atof(tok[2]);
        it.LatLonDegMin[2]=it.LatLonDegMin[0];
        it.LatLonDegMin[3]=it.LatLonDegMin[1];
        for(int k=0;k<4;k++) it.LatLonRad[k]=degmin2rad(it.LatLonDegMin[k]);
        vec_push(out_items,it);
      }
    }

    free_tokens(tok,nt);
  }

  fclose(fp);
  if (!found_ship) die("Ship not found in file (name mismatch?)");
  *out_shipCap = ShipCap;
}

/* Build Ex arrays: ship, stat, port, wayp (like Python) */
typedef struct {
  int SelectedSize;
  int Size; /* ship+stat+port */
  int *Type;
  int *ItemIndex;
  double *Amount;
  double *LatLonRad;     /* [SelectedSize][4] radians */
  double *LatLonDegMin;  /* [SelectedSize][4] degmin */
} ExData;

static void free_exdata(ExData *ex){
  free(ex->Type);
  free(ex->ItemIndex);
  free(ex->Amount);
  free(ex->LatLonRad);
  free(ex->LatLonDegMin);
}

static int write_distance_matrix_sqlite(const char *fname,
                                        const ItemVec *items,
                                        const ExData *ex,
                                        const double *FullDist,
                                        const int *FullFsb,
                                        int FullM)
{
  sqlite3 *db = NULL;
  sqlite3_stmt *stmt = NULL;
  int rc = sqlite3_open(fname, &db);
  if (rc != SQLITE_OK) {
    fprintf(stderr, "sqlite open failed: %s\n", db ? sqlite3_errmsg(db) : "unknown");
    if (db) sqlite3_close(db);
    return 1;
  }

  rc = sqlite3_exec(db,
                    "PRAGMA journal_mode=WAL;"
                    "DROP TABLE IF EXISTS legacy_distances;"
                    "CREATE TABLE legacy_distances ("
                    "from_node INTEGER,"
                    "to_node INTEGER,"
                    "from_loc_index INTEGER,"
                    "to_loc_index INTEGER,"
                    "from_side INTEGER,"
                    "to_side INTEGER,"
                    "from_type INTEGER,"
                    "to_type INTEGER,"
                    "from_name TEXT,"
                    "to_name TEXT,"
                    "from_lat_deg REAL,"
                    "from_lon_deg REAL,"
                    "to_lat_deg REAL,"
                    "to_lon_deg REAL,"
                    "distance_nm REAL,"
                    "feasible INTEGER"
                    ");"
                    "CREATE INDEX idx_legacy_from_to ON legacy_distances(from_node, to_node);"
                    "CREATE INDEX idx_legacy_coords ON legacy_distances(from_lat_deg, from_lon_deg, to_lat_deg, to_lon_deg);",
                    NULL, NULL, NULL);
  if (rc != SQLITE_OK) {
    fprintf(stderr, "sqlite schema setup failed: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return 1;
  }

  rc = sqlite3_prepare_v2(db,
                          "INSERT INTO legacy_distances ("
                          "from_node,to_node,from_loc_index,to_loc_index,from_side,to_side,"
                          "from_type,to_type,from_name,to_name,from_lat_deg,from_lon_deg,"
                          "to_lat_deg,to_lon_deg,distance_nm,feasible"
                          ") VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);",
                          -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    fprintf(stderr, "sqlite prepare failed: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return 1;
  }

  sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);

  for (int i = 0; i < FullM; i++) {
    int from_loc = i / 2;
    int from_side = i % 2;
    int from_item_idx = ex->ItemIndex[from_loc];
    const Item *from_item = &items->a[from_item_idx];
    double from_lat_deg = ex->LatLonRad[from_loc * 4 + (from_side == 0 ? 0 : 2)] * 180.0 / PI;
    double from_lon_deg = ex->LatLonRad[from_loc * 4 + (from_side == 0 ? 1 : 3)] * 180.0 / PI;

    for (int j = 0; j < FullM; j++) {
      int to_loc = j / 2;
      int to_side = j % 2;
      int to_item_idx = ex->ItemIndex[to_loc];
      const Item *to_item = &items->a[to_item_idx];
      double to_lat_deg = ex->LatLonRad[to_loc * 4 + (to_side == 0 ? 0 : 2)] * 180.0 / PI;
      double to_lon_deg = ex->LatLonRad[to_loc * 4 + (to_side == 0 ? 1 : 3)] * 180.0 / PI;

      sqlite3_bind_int(stmt, 1, i);
      sqlite3_bind_int(stmt, 2, j);
      sqlite3_bind_int(stmt, 3, from_loc);
      sqlite3_bind_int(stmt, 4, to_loc);
      sqlite3_bind_int(stmt, 5, from_side);
      sqlite3_bind_int(stmt, 6, to_side);
      sqlite3_bind_int(stmt, 7, ex->Type[from_loc]);
      sqlite3_bind_int(stmt, 8, ex->Type[to_loc]);
      sqlite3_bind_text(stmt, 9, from_item->Name ? from_item->Name : "", -1, SQLITE_STATIC);
      sqlite3_bind_text(stmt, 10, to_item->Name ? to_item->Name : "", -1, SQLITE_STATIC);
      sqlite3_bind_double(stmt, 11, from_lat_deg);
      sqlite3_bind_double(stmt, 12, from_lon_deg);
      sqlite3_bind_double(stmt, 13, to_lat_deg);
      sqlite3_bind_double(stmt, 14, to_lon_deg);
      sqlite3_bind_double(stmt, 15, FullDist[i * FullM + j]);
      sqlite3_bind_int(stmt, 16, FullFsb[i * FullM + j]);

      rc = sqlite3_step(stmt);
      if (rc != SQLITE_DONE) {
        fprintf(stderr, "sqlite insert failed: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        sqlite3_close(db);
        return 1;
      }
      sqlite3_reset(stmt);
      sqlite3_clear_bindings(stmt);
    }
  }

  sqlite3_finalize(stmt);
  sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
  sqlite3_close(db);
  return 0;
}

static void append_type_ex(const ItemVec *items, int typ, ExData *ex, int *kptr) {
  int k=*kptr;
  for(int i=0;i<items->n;i++){
    if(items->a[i].Type!=typ) continue;
    ex->Type[k]=items->a[i].Type;
    ex->ItemIndex[k]=i;
    ex->Amount[k]=items->a[i].Amount;
    for(int t=0;t<4;t++){
      ex->LatLonRad[k*4+t]=items->a[i].LatLonRad[t];
      ex->LatLonDegMin[k*4+t]=items->a[i].LatLonDegMin[t];
    }
    k++;
  }
  *kptr=k;
}

static ExData build_exdata(const ItemVec *items) {
  int c_ship=0,c_stat=0,c_port=0,c_wayp=0;
  for(int i=0;i<items->n;i++){
    if(items->a[i].Type==tSHIP) c_ship++;
    else if(items->a[i].Type==tSTAT) c_stat++;
    else if(items->a[i].Type==tPORT && items->a[i].PortSelected) c_port++;
    else if(items->a[i].Type==tWAYP) c_wayp++;
  }
  ExData ex;
  ex.SelectedSize = c_ship+c_stat+c_port+c_wayp;
  ex.Size = c_ship+c_stat+c_port;
  ex.Type = (int*)xmalloc((size_t)ex.SelectedSize*sizeof(int));
  ex.ItemIndex = (int*)xmalloc((size_t)ex.SelectedSize*sizeof(int));
  ex.Amount = (double*)xmalloc((size_t)ex.SelectedSize*sizeof(double));
  ex.LatLonRad = (double*)xmalloc((size_t)ex.SelectedSize*4*sizeof(double));
  ex.LatLonDegMin = (double*)xmalloc((size_t)ex.SelectedSize*4*sizeof(double));
  int k=0;
  append_type_ex(items,tSHIP,&ex,&k);
  append_type_ex(items,tSTAT,&ex,&k);
  /* ports participate in optimization only when selected */
  for(int i=0;i<items->n;i++){
    if(items->a[i].Type!=tPORT) continue;
    if(!items->a[i].PortSelected) continue;
    ex.Type[k]=items->a[i].Type;
    ex.ItemIndex[k]=i;
    ex.Amount[k]=items->a[i].Amount;
    for(int t=0;t<4;t++){
      ex.LatLonRad[k*4+t]=items->a[i].LatLonRad[t];
      ex.LatLonDegMin[k*4+t]=items->a[i].LatLonDegMin[t];
    }
    k++;
  }
  append_type_ex(items,tWAYP,&ex,&k);
  if(k!=ex.SelectedSize) die("build_exdata mismatch");
  return ex;
}

static void build_waypoint_dist(const ExData *ex,
                                const double *Land, int nLand,
                                double **out_dist, int **out_fsb,
                                double **out_full_dist, int **out_full_fsb,
                                int *out_full_m) {
  (void)Land;
  (void)nLand;

  int m = ex->SelectedSize;
  int M = 2 * ex->SelectedSize;
  int n = 2 * ex->Size;

  int *F = (int*)xcalloc((size_t)M * (size_t)M, sizeof(int));
  double *D = (double*)xcalloc((size_t)M * (size_t)M, sizeof(double));

  double *latlon_cols[4];
  for (int k = 0; k < 4; k++) {
    latlon_cols[k] = (double*)xmalloc((size_t)m * sizeof(double));
  }
  for (int i = 0; i < m; i++) {
    latlon_cols[0][i] = ex->LatLonRad[i*4 + 0];
    latlon_cols[1][i] = ex->LatLonRad[i*4 + 1];
    latlon_cols[2][i] = ex->LatLonRad[i*4 + 2];
    latlon_cols[3][i] = ex->LatLonRad[i*4 + 3];
  }

  int *type_main = (int*)xmalloc((size_t)ex->Size * sizeof(int));
  for (int i = 0; i < ex->Size; i++) type_main[i] = ex->Type[i];

  double start_end[4] = {
    ex->LatLonRad[0], ex->LatLonRad[1],
    ex->LatLonRad[2], ex->LatLonRad[3]
  };

  if (DistanceLink(D, F, type_main, latlon_cols, start_end, ex->Size, ex->SelectedSize) != 0) {
    die("DistanceLink failed (could not read map?)");
  }

  for (int k = 0; k < 4; k++) free(latlon_cols[k]);
  free(type_main);

  double *dist = (double*)xmalloc((size_t)n * (size_t)n * sizeof(double));
  int *fsb = (int*)xmalloc((size_t)n * (size_t)n * sizeof(int));
  for (int i = 0; i < n; i++) {
    for (int j = 0; j < n; j++) {
      dist[i*n + j] = D[i*M + j];
      fsb[i*n + j] = F[i*M + j];
    }
  }

  if (out_full_dist) *out_full_dist = D;
  else free(D);
  if (out_full_fsb) *out_full_fsb = F;
  else free(F);
  if (out_full_m) *out_full_m = M;

  *out_dist = dist;
  *out_fsb = fsb;
}

static void write_route_dat(const char *fname, const ItemVec *items, const ExData *ex,
                            const int *letour, int letour_len) {
  FILE *fp = fopen(fname, "w");
  if (!fp) { perror("fopen(write_route_dat)"); return; }

  int ship_ex = 0;
  Item *ship = &items->a[ex->ItemIndex[ship_ex]];
  if (ship->RawLine) {
    fputs(ship->RawLine, fp);
    if (ship->RawLine[strlen(ship->RawLine)-1] != '\n') fputc('\n', fp);
  } else {
    fprintf(fp, "BOAT %.0f %.0f %.0f %.0f %.0f %.0f %.0f %.0f %.0f %.0f %.0f \"%s\"",
            ship->BoatData[0], ship->BoatData[1], ship->BoatData[2], ship->BoatData[3],
            ship->BoatData[4], ship->BoatData[5], ship->BoatData[6], ship->BoatData[7],
            ship->BoatData[8], ship->BoatData[9], ship->BoatData[10], ship->Name);
    if (ship->Comment) fprintf(fp, " %s", ship->Comment);
    fputc('\n', fp);
  }

  for (int i = 0; i < letour_len; i++) {
    int idx = letour[i];
    if (idx == 0) continue;
    int ex_idx = abs(idx);
    int typ = ex->Type[ex_idx];
    if (typ == tSTAT) {
      Item *st = &items->a[ex->ItemIndex[ex_idx]];
      int sr;
      if (st->Fixed) sr = (idx < 0) ? 3 : 2;
      else sr = (idx < 0) ? 1 : 0;

      int dm0 = (int)llround(st->LatLonDegMin[0]);
      int dm1 = (int)llround(st->LatLonDegMin[1]);
      int dm2 = (int)llround(st->LatLonDegMin[2]);
      int dm3 = (int)llround(st->LatLonDegMin[3]);

      fprintf(fp, "STAT %d %d %d %d %d %d %d %.0f %.0f",
              st->Reitur, st->Tog, sr, dm0, dm1, dm2, dm3,
              st->Amount, st->ExtraTime);
      if (st->Comment) fprintf(fp, " %s", st->Comment);
      fputc('\n', fp);
    } else if (typ == tPORT) {
      Item *pt = &items->a[ex->ItemIndex[ex_idx]];
      int dm0 = (int)llround(pt->LatLonDegMin[0]);
      int dm1 = (int)llround(pt->LatLonDegMin[1]);
      fprintf(fp, "PORT %d %d %s %d", dm0, dm1, pt->Name,
              pt->PortSelected ? 1 : 0);
      if (pt->Comment) fprintf(fp, " %s", pt->Comment);
      fputc('\n', fp);
    }
  }

  /* unique inactive ports, all deselected */
  for (int i = 0; i < items->n; i++) {
    if (items->a[i].Type != tPORT) continue;
    if (items->a[i].PortSelected) continue;
    int seen = 0;
    for (int j = 0; j < i; j++) {
      if (items->a[j].Type != tPORT) continue;
      if (items->a[j].PortSelected) continue;
      if (items->a[j].Name && items->a[i].Name &&
          strcmp(items->a[j].Name, items->a[i].Name) == 0) {
        seen = 1;
        break;
      }
    }
    if (seen) continue;
    int dm0 = (int)llround(items->a[i].LatLonDegMin[0]);
    int dm1 = (int)llround(items->a[i].LatLonDegMin[1]);
    fprintf(fp, "PORT %d %d %s 0", dm0, dm1, items->a[i].Name);
    if (items->a[i].Comment) fprintf(fp, " %s", items->a[i].Comment);
    fputc('\n', fp);
  }

  /* all waypoints */
  for (int i = 0; i < items->n; i++) {
    if (items->a[i].Type != tWAYP) continue;
    if (items->a[i].RawLine) {
      fputs(items->a[i].RawLine, fp);
      if (items->a[i].RawLine[strlen(items->a[i].RawLine)-1] != '\n') fputc('\n', fp);
    } else {
      int dm0 = (int)llround(items->a[i].LatLonDegMin[0]);
      int dm1 = (int)llround(items->a[i].LatLonDegMin[1]);
      fprintf(fp, "WAYP %d %d 1\n", dm0, dm1);
    }
  }

  fclose(fp);
}

/* ---------- Land data loading (island.bin) ---------- */
/* Reads float32 array of length 2*n: [lat0..latn-1, lon0..lonn-1] */
static double *load_island_bin(const char *fname, int *out_n) {
  FILE *fp = fopen(fname, "rb");
  if(!fp){ perror("fopen island.bin"); exit(1); }
  fseek(fp,0,SEEK_END);
  long bytes = ftell(fp);
  fseek(fp,0,SEEK_SET);
  if (bytes <= 0 || (bytes % 4) != 0) die("island.bin size invalid");
  long nfloat = bytes / 4;
  if ((nfloat % 2) != 0) die("island.bin float count must be even");
  int n = (int)(nfloat/2);

  float *buf = (float*)xmalloc((size_t)nfloat*sizeof(float));
  if (fread(buf, sizeof(float), (size_t)nfloat, fp) != (size_t)nfloat) die("Failed to read island.bin");
  fclose(fp);

  double *Land = (double*)xmalloc((size_t)nfloat*sizeof(double));
  for(long i=0;i<nfloat;i++) Land[i] = (double)buf[i];
  free(buf);

  *out_n = n;
  return Land;
}

/* ---------- Subtour + callback ---------- */
struct callback_data { int n; };

static void findsubtour_directed(int n, double *sol, int *tourlenP, int *tour) {
  int *unvis=(int*)xcalloc((size_t)n,sizeof(int));
  int *best=(int*)xmalloc((size_t)n*sizeof(int));
  int *thisc=(int*)xmalloc((size_t)n*sizeof(int));
  for(int i=0;i<n;i++) unvis[i]=1;

  int bestlen=n+1, remaining=n;
  while(remaining>0){
    int start=-1;
    for(int i=0;i<n;i++) if(unvis[i]){ start=i; break; }
    if(start<0) break;
    int len=0, cur=start;
    while(cur>=0 && unvis[cur]){
      thisc[len++]=cur;
      unvis[cur]=0; remaining--;
      int next=-1;
      for(int j=0;j<n;j++){
        if(sol[cur*n+j]>0.5 && unvis[j]){ next=j; break; }
      }
      cur=next;
    }
    if(len<bestlen){
      bestlen=len;
      for(int i=0;i<len;i++) best[i]=thisc[i];
    }
  }
  for(int i=0;i<bestlen;i++) tour[i]=best[i];
  *tourlenP=bestlen;
  free(unvis); free(best); free(thisc);
}

static int *node_tour_to_letour(const int *tour, int len, int Size, int *out_len) {
  int *letour = (int*)xmalloc((size_t)Size * sizeof(int));
  int count = 0;
  for (int i = 0; i < len; i++) {
    int city = tour[i] / 2;
    int seen = 0;
    for (int j = 0; j < count; j++) {
      if (abs(letour[j]) == city) { seen = 1; break; }
    }
    if (!seen) {
      letour[count++] = (tour[i] % 2 == 1) ? -city : city;
    }
  }

  int k0 = -1;
  for (int i = 0; i < count; i++) {
    if (letour[i] == 0) { k0 = i; break; }
  }
  if (k0 > 0) {
    int *rot = (int*)xmalloc((size_t)count * sizeof(int));
    int idx = 0;
    for (int i = k0; i < count; i++) rot[idx++] = letour[i];
    for (int i = 0; i < k0; i++) rot[idx++] = letour[i];
    memcpy(letour, rot, (size_t)count * sizeof(int));
    free(rot);
  }

  *out_len = count;
  return letour;
}


int __stdcall subtourelim(GRBmodel *model, void *cbdata, int where, void *usrdata) {
  (void)model;
  struct callback_data *d=(struct callback_data*)usrdata;
  int n=d->n;
  int error=0;

  if(where==GRB_CB_MIPSOL){
    double *sol=(double*)xmalloc((size_t)n*n*sizeof(double));
    int *tour=(int*)xmalloc((size_t)n*sizeof(int));
    GRBcbget(cbdata, where, GRB_CB_MIPSOL_SOL, sol);

    int len;
    findsubtour_directed(n, sol, &len, tour);

    if(len<n){
      int maxPairs=len*(len-1)/2;
      int nz=2*maxPairs;
      int *ind=(int*)xmalloc((size_t)nz*sizeof(int));
      double *val=(double*)xmalloc((size_t)nz*sizeof(double));
      int k=0;
      for(int a=0;a<len;a++){
        for(int b=a+1;b<len;b++){
          int i=tour[a], j=tour[b];
          ind[k]=i*n+j; val[k]=1.0; k++;
          ind[k]=j*n+i; val[k]=1.0; k++;
        }
      }
      error = GRBcblazy(cbdata, k, ind, val, GRB_LESS_EQUAL, (double)len-1.0);
      free(ind); free(val);
    }
    free(sol); free(tour);
  }
  return error;
}

/* ---------- Main ---------- */
int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "Usage: %s <datafile.dat> <ship_id 1..4> [timelimit_seconds] [--no-ports] [--write-dat <out.dat>] [--write-matrix <out.sqlite>]\n", argv[0]);
    return 1;
  }

  const char *file = argv[1];
  int ship_id = atoi(argv[2]);
  double timelimit = 3600.0;
  int skip_ports = 0;
  const char *write_dat = NULL;
  const char *write_matrix = NULL;
  for (int i = 3; i < argc; i++) {
    if (strcmp(argv[i], "--no-ports") == 0) {
      skip_ports = 1;
    } else if (strcmp(argv[i], "--write-dat") == 0) {
      if (i + 1 < argc) {
        write_dat = argv[i + 1];
        i++;
      } else {
        die("--write-dat requires a path");
      }
    } else if (strcmp(argv[i], "--write-matrix") == 0) {
      if (i + 1 < argc) {
        write_matrix = argv[i + 1];
        i++;
      } else {
        die("--write-matrix requires a path");
      }
    } else {
      timelimit = atof(argv[i]);
    }
  }

  const char *ship_names[] = { "Árni Friðriksson", "Bjarni Sæmundsson", "Gullver", "Breki" };
  if (ship_id < 1 || ship_id > 4) die("ship_id must be 1..4");
  const char *ship = ship_names[ship_id-1];

  printf("Ship: %s\n", ship);

  /* read dat */
  ItemVec items; vec_init(&items);
  double ShipCap = 0.0;
  readDat_C(file, ship, &items, &ShipCap, skip_ports);
  printf("ShipCap: %.0f\n", ShipCap);

  ExData ex = build_exdata(&items);
  printf("SelectedSize=%d  Size=%d\n", ex.SelectedSize, ex.Size);

  /* Build waypoint-aware feasibility + distance for node graph of size n = 2*Size */
  int Size = ex.Size;
  int n = 2 * Size;

  double *dist = NULL;
  int *fsb = NULL;
  double *full_dist = NULL;
  int *full_fsb = NULL;
  int full_m = 0;
  build_waypoint_dist(&ex, NULL, 0, &dist, &fsb, &full_dist, &full_fsb, &full_m);

  if (write_matrix) {
    if (write_distance_matrix_sqlite(write_matrix, &items, &ex, full_dist, full_fsb, full_m) != 0) {
      die("Failed to write legacy distance matrix export");
    }
    printf("Legacy distance matrix written to %s\n", write_matrix);
  }

  /* special closure between nodes 0 and 1 */
  dist[0*n + 1] = 0.0;
  dist[1*n + 0] = 0.0;
  fsb[0*n + 1] = 1;
  fsb[1*n + 0] = 1;

  /* Create Gurobi model (same as Python: full NxN vars) */
  GRBenv *env = NULL;
  GRBmodel *model = NULL;
  int error = 0;

  error = GRBloadenv(&env, "planner.log");
  if (error) goto QUIT;
  GRBsetintparam(env, "OutputFlag", 1);
  GRBsetintparam(env, "LogToConsole", 1);

  error = GRBnewmodel(env, &model, "planner", 0, NULL, NULL, NULL, NULL, NULL);
  if (error) goto QUIT;

  GRBsetdblparam(env, "TimeLimit", timelimit);
  GRBsetintparam(env, "Threads", 4);

  /* vars e[i,j] index = i*n + j */
  for (int i = 0; i < n; i++) {
    for (int j = 0; j < n; j++) {
      char vname[64];
      sprintf(vname, "e_%d_%d", i, j);
      error = GRBaddvar(model, 0, NULL, NULL, dist[i*n + j], 0.0, 1.0, GRB_BINARY, vname);
      if (error) goto QUIT;
    }
  }

#ifdef USE_AMOUNT
  int n2 = n / 2;
  int xcount = n * n;
  int w_offset = xcount;
  int v_offset = xcount + xcount;

  for (int i = 0; i < n; i++) {
    for (int j = 0; j < n; j++) {
      char vname[64];
      sprintf(vname, "w_%d_%d", i, j);
      error = GRBaddvar(model, 0, NULL, NULL, 0.0, 0.0, ShipCap, GRB_CONTINUOUS, vname);
      if (error) goto QUIT;
    }
  }
  for (int i = 0; i < n; i++) {
    for (int j = 0; j < n; j++) {
      char vname[64];
      int k = i / 2;
      int allow_v = (i != j) && (i / 2 == j / 2) && (ex.Type[k] != tPORT);
      double ub = allow_v ? ShipCap : 0.0;
      sprintf(vname, "v_%d_%d", i, j);
      error = GRBaddvar(model, 0, NULL, NULL, 0.0, 0.0, ub, GRB_CONTINUOUS, vname);
      if (error) goto QUIT;
    }
  }
#endif

  /* outdegree=1 and indegree=1 */
  int *ind = (int*)xmalloc((size_t)n * sizeof(int));
  double *val = (double*)xmalloc((size_t)n * sizeof(double));
  for (int j = 0; j < n; j++) val[j] = 1.0;

  for (int i = 0; i < n; i++) {
    for (int j = 0; j < n; j++) ind[j] = i*n + j;
    char cname[64];
    sprintf(cname, "out_%d", i);
    error = GRBaddconstr(model, n, ind, val, GRB_EQUAL, 1.0, cname);
    if (error) goto QUIT;
  }
  for (int j = 0; j < n; j++) {
    for (int i = 0; i < n; i++) ind[i] = i*n + j;
    char cname[64];
    sprintf(cname, "in_%d", j);
    error = GRBaddconstr(model, n, ind, val, GRB_EQUAL, 1.0, cname);
    if (error) goto QUIT;
  }

  /* internal constraint: e[2i,2i+1]+e[2i+1,2i]==1 */
  for (int i = 0; i < Size; i++) {
    int a = 2*i, b = 2*i+1;
    ind[0] = a*n + b; val[0] = 1.0;
    ind[1] = b*n + a; val[1] = 1.0;
    char cname[64];
    sprintf(cname, "pair_%d", i);
    error = GRBaddconstr(model, 2, ind, val, GRB_EQUAL, 1.0, cname);
    if (error) goto QUIT;
  }
  free(ind); free(val);

  /* forbid self loops */
  for (int i = 0; i < n; i++) {
    error = GRBsetdblattrelement(model, GRB_DBL_ATTR_UB, i*n + i, 0.0);
    if (error) goto QUIT;
  }

  /* Do not forbid land-crossing arcs; distances already reroute via waypoints. */

#ifdef USE_AMOUNT
  int *ind2 = (int*)xmalloc((size_t)(n + 4) * sizeof(int));
  double *val2 = (double*)xmalloc((size_t)(n + 4) * sizeof(double));

  for (int j = 0; j < n2; j++) {
    if (ex.Type[j] == tPORT) continue;
    int nnz = 0;
    for (int k = 0; k < n; k++) {
      if (k == 2*j + 1) continue;
      ind2[nnz] = w_offset + k*n + 2*j;
      val2[nnz++] = 1.0;
    }
    ind2[nnz] = v_offset + (2*j)*n + (2*j+1); val2[nnz++] = 1.0;
    ind2[nnz] = w_offset + (2*j)*n + (2*j+1); val2[nnz++] = -1.0;
    error = GRBaddconstr(model, nnz, ind2, val2, GRB_EQUAL, 0.0, NULL);
    if (error) goto QUIT;

    nnz = 0;
    for (int k = 0; k < n; k++) {
      if (k == 2*j) continue;
      ind2[nnz] = w_offset + k*n + (2*j+1);
      val2[nnz++] = 1.0;
    }
    ind2[nnz] = v_offset + (2*j+1)*n + (2*j); val2[nnz++] = 1.0;
    ind2[nnz] = w_offset + (2*j+1)*n + (2*j); val2[nnz++] = -1.0;
    error = GRBaddconstr(model, nnz, ind2, val2, GRB_EQUAL, 0.0, NULL);
    if (error) goto QUIT;

    ind2[0] = v_offset + (2*j)*n + (2*j+1); val2[0] = 1.0;
    ind2[1] = v_offset + (2*j+1)*n + (2*j); val2[1] = 1.0;
    error = GRBaddconstr(model, 2, ind2, val2, GRB_EQUAL, ex.Amount[j], NULL);
    if (error) goto QUIT;

    nnz = 0;
    ind2[nnz] = w_offset + (2*j)*n + (2*j+1); val2[nnz++] = 1.0;
    for (int k = 0; k < n; k++) {
      if (k == 2*j || k == 2*j+1) continue;
      ind2[nnz] = w_offset + (2*j+1)*n + k;
      val2[nnz++] = -1.0;
    }
    error = GRBaddconstr(model, nnz, ind2, val2, GRB_EQUAL, 0.0, NULL);
    if (error) goto QUIT;

    nnz = 0;
    ind2[nnz] = w_offset + (2*j+1)*n + (2*j); val2[nnz++] = 1.0;
    for (int k = 0; k < n; k++) {
      if (k == 2*j || k == 2*j+1) continue;
      ind2[nnz] = w_offset + (2*j)*n + k;
      val2[nnz++] = -1.0;
    }
    error = GRBaddconstr(model, nnz, ind2, val2, GRB_EQUAL, 0.0, NULL);
    if (error) goto QUIT;
  }

  for (int i = 0; i < n; i++) {
    for (int j = 0; j < n; j++) {
      ind2[0] = w_offset + i*n + j; val2[0] = 1.0;
      ind2[1] = i*n + j; val2[1] = -ShipCap;
      error = GRBaddconstr(model, 2, ind2, val2, GRB_LESS_EQUAL, 0.0, NULL);
      if (error) goto QUIT;
    }
  }

  for (int j = 0; j < n2; j++) {
    if (ex.Type[j] == tPORT) continue;
    int a = 2*j, b = 2*j+1;
    ind2[0] = v_offset + a*n + b; val2[0] = 1.0;
    ind2[1] = a*n + b; val2[1] = -ShipCap;
    error = GRBaddconstr(model, 2, ind2, val2, GRB_LESS_EQUAL, 0.0, NULL);
    if (error) goto QUIT;
    ind2[0] = v_offset + b*n + a; val2[0] = 1.0;
    ind2[1] = b*n + a; val2[1] = -ShipCap;
    error = GRBaddconstr(model, 2, ind2, val2, GRB_LESS_EQUAL, 0.0, NULL);
    if (error) goto QUIT;
  }

  for (int i = 0; i < n; i++) {
    if (ex.Type[i/2] != tPORT) continue;
    for (int j = 0; j < n; j++) {
      ind2[0] = w_offset + i*n + j; val2[0] = 1.0;
      error = GRBaddconstr(model, 1, ind2, val2, GRB_EQUAL, 0.0, NULL);
      if (error) goto QUIT;
    }
  }

  free(ind2);
  free(val2);
#endif

  /* callback */
  struct callback_data cb; cb.n = n;
  error = GRBsetcallbackfunc(model, subtourelim, (void*)&cb);
  if (error) goto QUIT;
  error = GRBsetintparam(GRBgetenv(model), GRB_INT_PAR_LAZYCONSTRAINTS, 1);
  if (error) goto QUIT;

  error = GRBoptimize(model);
  if (error) goto QUIT;

  int solcount = 0;
  error = GRBgetintattr(model, GRB_INT_ATTR_SOLCOUNT, &solcount);
  if (error) goto QUIT;

  if (solcount > 0) {
    double *sol = (double*)xmalloc((size_t)n*n*sizeof(double));
    error = GRBgetdblattrarray(model, GRB_DBL_ATTR_X, 0, n*n, sol);
    if (error) goto QUIT;

    int *tour = (int*)xmalloc((size_t)n*sizeof(int));
    int len = 0;
    findsubtour_directed(n, sol, &len, tour);

    printf("Tour (nodes): ");
    for (int i = 0; i < len; i++) printf("%d ", tour[i]);
    printf("\n");

    int *pairDir = (int*)xmalloc((size_t)Size * sizeof(int));
    for (int c = 0; c < Size; c++) {
      int a = 2*c, b = 2*c+1;
      if (sol[a*n + b] > 0.5) pairDir[c] = +1;
      else pairDir[c] = -1; /* due to constraint, one must be 1 */
    }

    char *plot_path = NULL;
    if (write_dat) {
      const char *dot = strrchr(write_dat, '.');
      size_t base = dot ? (size_t)(dot - write_dat) : strlen(write_dat);
      plot_path = (char*)xmalloc(base + 5);
      memcpy(plot_path, write_dat, base);
      memcpy(plot_path + base, ".txt", 5);
    }
    const char *plot_out = plot_path ? plot_path : "solution_plot.txt";
    write_plot_bundle(plot_out,
                      Size, ex.SelectedSize,
                      tour, len,
                      pairDir,
                      ex.Type, ex.Amount, ex.LatLonRad,
                      full_dist, full_fsb, full_m);
    free(plot_path);

    if (write_dat) {
      int letour_len = 0;
      int *letour = node_tour_to_letour(tour, len, Size, &letour_len);
      write_route_dat(write_dat, &items, &ex, letour, letour_len);
      free(letour);
    }

    double obj = 0.0;
    GRBgetdblattr(model, GRB_DBL_ATTR_OBJVAL, &obj);
    printf("Objective: %g\n", obj);

    free(tour);
    free(sol);
  } else {
    printf("No solution found.\n");
  }

QUIT:
  if (error) fprintf(stderr, "ERROR: %s\n", GRBgeterrormsg(env));
  if (model) GRBfreemodel(model);
  if (env) GRBfreeenv(env);

  free(dist);
  free(fsb);
  free(full_dist);
  free(full_fsb);
  free_exdata(&ex);
  vec_free(&items);
  return error ? 1 : 0;
}
