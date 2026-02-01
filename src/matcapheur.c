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
#include <time.h>

#include "gurobi_c.h"

extern int DistanceLink(double *DistrMtrx, int *FsbleMtrx, int *Type,
                        double *LatLon[4], double *StartEnd,
                        int Size, int SelectedSize);

typedef struct ItemVec ItemVec;
typedef struct ExData ExData;

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

static void write_name_line(FILE *fp, const char *name) {
  if (!name || !*name) {
    fputs("\"\"", fp);
    return;
  }
  size_t n = strlen(name);
  if (name[0] == '"' && name[n-1] == '"') {
    fputs(name, fp);
  } else if (strchr(name, ' ') || strchr(name, '\t')) {
    fprintf(fp, "\"%s\"", name);
  } else {
    fputs(name, fp);
  }
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

typedef struct ItemVec {
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
typedef struct ExData {
  int SelectedSize;
  int Size; /* ship+stat+port */
  int *Type;
  int *ItemIndex;
  double *Amount;
  double *LatLonRad;     /* [SelectedSize][4] radians */
  double *LatLonDegMin;  /* [SelectedSize][4] degmin */
} ExData;

typedef struct {
  int type;    /* tSTAT or tPORT */
  int ex_idx;  /* index into original ExData */
} Visit;

typedef struct {
  double distance;
  double total_amount;
  int n_stations;
  int start_ex;
  int end_ex;
  int start_type;
  int end_type;
} SegmentResult;

static int solve_segment_distance(GRBenv *env, const ExData *orig,
                                  const int *station_ex, int n_station,
                                  const double start_rad[2], const double end_rad[2],
                                  double timelimit, double *out_dist,
                                  int **out_order, int *out_order_len);

typedef struct {
  SegmentResult res;
  int *order;
  int order_len;
} SegmentEval;

typedef struct {
  int start_ex;
  int start_type;
  int end_ex;
  int end_type;
  int start_idx;
  int end_idx;
  int n_stations;
  double total_amount;
} SegmentInfo;

static double sum_segment_distance(const SegmentResult *segs, int nseg) {
  double total = 0.0;
  for (int i = 0; i < nseg; i++) {
    if (segs[i].distance < 0.0) return -1.0;
    total += segs[i].distance;
  }
  return total;
}

static double letour_distance_total(const int *letour, int letour_len,
                                    const double *dist, int Size) {
  if (!letour || letour_len <= 0) return -1.0;
  int n = 2 * Size;
  double total = 0.0;
  int prev_node = 0;
  for (int i = 1; i < letour_len; i++) {
    int idx = letour[i];
    if (idx == 0) continue;
    int k = abs(idx);
    int entry = 2 * k + (idx < 0 ? 1 : 0);
    int exit_node = 2 * k + (idx < 0 ? 0 : 1);
    total += dist[prev_node * n + entry];
    total += dist[entry * n + exit_node];
    prev_node = exit_node;
  }
  total += dist[prev_node * n + 1];
  return total;
}

static void free_segment_eval(SegmentEval *evals, int nseg) {
  if (!evals) return;
  for (int i = 0; i < nseg; i++) free(evals[i].order);
  free(evals);
}

static SegmentEval *build_segment_eval_from_tour(const SegmentResult *segs, int nseg,
                                                 const int *seg_tour, int seg_tour_len,
                                                 const ExData *ex) {
  SegmentEval *evals = (SegmentEval*)xcalloc((size_t)nseg, sizeof(SegmentEval));
  for (int i = 0; i < nseg; i++) evals[i].res = segs[i];
  if (!seg_tour || seg_tour_len <= 0) return evals;

  int *tmp = (int*)xmalloc((size_t)seg_tour_len * sizeof(int));
  int seg = 0;
  int count = 0;
  for (int i = 1; i < seg_tour_len; i++) {
    int idx = seg_tour[i];
    if (idx == 0) continue;
    if (ex->Type[abs(idx)] == tPORT) {
      evals[seg].order_len = count;
      if (count > 0) {
        evals[seg].order = (int*)xmalloc((size_t)count * sizeof(int));
        memcpy(evals[seg].order, tmp, (size_t)count * sizeof(int));
      }
      count = 0;
      seg++;
      continue;
    }
    tmp[count++] = idx;
  }
  if (seg < nseg) {
    evals[seg].order_len = count;
    if (count > 0) {
      evals[seg].order = (int*)xmalloc((size_t)count * sizeof(int));
      memcpy(evals[seg].order, tmp, (size_t)count * sizeof(int));
    }
  }
  free(tmp);
  return evals;
}

static SegmentInfo *build_segment_info(const Visit *visits, int n_visits, const ExData *ex,
                                       int *out_nseg) {
  int max_seg = 1;
  for (int i = 0; i < n_visits; i++) if (visits[i].type == tPORT) max_seg++;
  SegmentInfo *info = (SegmentInfo*)xcalloc((size_t)max_seg, sizeof(SegmentInfo));

  int seg = 0;
  int start_idx = 0;
  int start_ex = 0;
  int start_type = tSHIP;
  double seg_amount = 0.0;
  int seg_stations = 0;

  for (int i = 0; i < n_visits; i++) {
    if (visits[i].type == tSTAT) {
      seg_amount += ex->Amount[visits[i].ex_idx];
      seg_stations++;
      continue;
    }
    if (visits[i].type != tPORT) continue;

    info[seg].start_idx = start_idx;
    info[seg].end_idx = i;
    info[seg].start_ex = start_ex;
    info[seg].start_type = start_type;
    info[seg].end_ex = visits[i].ex_idx;
    info[seg].end_type = tPORT;
    info[seg].total_amount = seg_amount;
    info[seg].n_stations = seg_stations;

    seg++;
    start_idx = i + 1;
    start_ex = visits[i].ex_idx;
    start_type = tPORT;
    seg_amount = 0.0;
    seg_stations = 0;
  }

  info[seg].start_idx = start_idx;
  info[seg].end_idx = n_visits;
  info[seg].start_ex = start_ex;
  info[seg].start_type = start_type;
  info[seg].end_ex = 0;
  info[seg].end_type = tSHIP;
  info[seg].total_amount = seg_amount;
  info[seg].n_stations = seg_stations;
  seg++;

  *out_nseg = seg;
  return info;
}

static int *collect_segment_stations(const Visit *visits, int start_idx, int end_idx,
                                     int n_stations, int *out_n) {
  int *station_ex = NULL;
  if (n_stations > 0) station_ex = (int*)xmalloc((size_t)n_stations * sizeof(int));
  int count = 0;
  for (int i = start_idx; i < end_idx; i++) {
    if (visits[i].type != tSTAT) continue;
    if (station_ex) station_ex[count] = visits[i].ex_idx;
    count++;
  }
  *out_n = count;
  return station_ex;
}

static SegmentEval eval_one_segment(GRBenv *env, const ExData *ex,
                                    const SegmentInfo *info, const Visit *visits,
                                    double timelimit) {
  SegmentEval eval;
  memset(&eval, 0, sizeof(eval));
  eval.res.start_ex = info->start_ex;
  eval.res.start_type = info->start_type;
  eval.res.end_ex = info->end_ex;
  eval.res.end_type = info->end_type;
  eval.res.total_amount = info->total_amount;
  eval.res.n_stations = info->n_stations;

  double start_rad[2];
  double end_rad[2];
  if (info->start_type == tSHIP) {
    start_rad[0] = ex->LatLonRad[0];
    start_rad[1] = ex->LatLonRad[1];
  } else {
    start_rad[0] = ex->LatLonRad[info->start_ex*4 + 0];
    start_rad[1] = ex->LatLonRad[info->start_ex*4 + 1];
  }
  if (info->end_type == tSHIP) {
    end_rad[0] = ex->LatLonRad[2];
    end_rad[1] = ex->LatLonRad[3];
  } else {
    end_rad[0] = ex->LatLonRad[info->end_ex*4 + 0];
    end_rad[1] = ex->LatLonRad[info->end_ex*4 + 1];
  }

  int n_station = 0;
  int *station_ex = collect_segment_stations(visits, info->start_idx, info->end_idx,
                                             info->n_stations, &n_station);
  double dist = 0.0;
  int *seg_order = NULL;
  int seg_order_len = 0;
  if (solve_segment_distance(env, ex, station_ex, n_station, start_rad, end_rad,
                             timelimit, &dist, &seg_order, &seg_order_len) != 0) {
    dist = -1.0;
  }
  free(station_ex);

  eval.res.distance = dist;
  eval.order = seg_order;
  eval.order_len = seg_order_len;
  return eval;
}

static Visit *build_visits_from_tour(const int *tour, int tour_len,
                                     const ExData *ex, int *out_n) {
  if (!tour || tour_len <= 0) { *out_n = 0; return NULL; }
  int cap = tour_len;
  int n = 0;
  Visit *visits = (Visit*)xmalloc((size_t)cap * sizeof(Visit));
  for (int i = 0; i < tour_len; i++) {
    int idx = tour[i];
    if (idx == 0) continue;
    int k = abs(idx);
    if (k <= 0 || k >= ex->SelectedSize) continue;
    if (ex->Type[k] == tSTAT) {
      visits[n].type = tSTAT;
      visits[n].ex_idx = k;
      n++;
    } else if (ex->Type[k] == tPORT) {
      visits[n].type = tPORT;
      visits[n].ex_idx = k;
      n++;
    }
  }
  *out_n = n;
  return visits;
}

static int *build_tour_from_eval(const SegmentEval *evals, int nseg, int *out_len);

static int evaluate_visits(const Visit *visits, int n_visits, const ExData *ex,
                           const SegmentEval *seg_eval_ref, int nseg_ref,
                           double timelimit, const double *dist, int Size,
                           SegmentEval **out_eval, int *out_nseg,
                           int **out_tour, int *out_tour_len,
                           double *out_total) {
  if (out_eval) *out_eval = NULL;
  if (out_nseg) *out_nseg = 0;
  if (out_tour) *out_tour = NULL;
  if (out_tour_len) *out_tour_len = 0;
  if (out_total) *out_total = -1.0;

  int nseg2 = 0;
  SegmentInfo *info = build_segment_info(visits, n_visits, ex, &nseg2);
  if (!info) return 0;

  GRBenv *seg_env = NULL;
  if (GRBloadenv(&seg_env, NULL) != 0) {
    free(info);
    return 0;
  }
  GRBsetintparam(seg_env, "OutputFlag", 0);
  GRBsetintparam(seg_env, "LogToConsole", 0);

  SegmentEval *trial_eval = (SegmentEval*)xcalloc((size_t)nseg2, sizeof(SegmentEval));
  int seg_count_changed = (nseg2 != nseg_ref) || !seg_eval_ref;
  for (int s = 0; s < nseg2; s++) {
    if (!seg_count_changed &&
        info[s].total_amount == seg_eval_ref[s].res.total_amount &&
        info[s].n_stations == seg_eval_ref[s].res.n_stations &&
        info[s].start_ex == seg_eval_ref[s].res.start_ex &&
        info[s].end_ex == seg_eval_ref[s].res.end_ex &&
        info[s].start_type == seg_eval_ref[s].res.start_type &&
        info[s].end_type == seg_eval_ref[s].res.end_type) {
      trial_eval[s].res = seg_eval_ref[s].res;
      trial_eval[s].order_len = seg_eval_ref[s].order_len;
      if (trial_eval[s].order_len > 0) {
        trial_eval[s].order = (int*)xmalloc((size_t)trial_eval[s].order_len * sizeof(int));
        memcpy(trial_eval[s].order, seg_eval_ref[s].order,
               (size_t)trial_eval[s].order_len * sizeof(int));
      }
    } else {
      trial_eval[s] = eval_one_segment(seg_env, ex, &info[s], visits, timelimit);
    }
  }
  GRBfreeenv(seg_env);
  free(info);

  int valid = 1;
  for (int s = 0; s < nseg2; s++) {
    if (trial_eval[s].res.distance < 0.0) { valid = 0; break; }
  }

  int *trial_tour = NULL;
  int trial_tour_len = 0;
  double trial_total = -1.0;
  if (valid) {
    trial_tour = build_tour_from_eval(trial_eval, nseg2, &trial_tour_len);
    trial_total = letour_distance_total(trial_tour, trial_tour_len, dist, Size);
  }

  if (out_eval) *out_eval = trial_eval;
  if (out_nseg) *out_nseg = nseg2;
  if (out_tour) *out_tour = trial_tour;
  if (out_tour_len) *out_tour_len = trial_tour_len;
  if (out_total) *out_total = trial_total;
  return valid;
}

static int optimize_port_boundaries(Visit **visits_io, int *n_visits_io,
                                    const ExData *ex, const double *dist, int Size,
                                    double ship_cap, double timelimit,
                                    int max_iters) {
  if (!visits_io || !*visits_io || !n_visits_io || *n_visits_io <= 0) return 0;
  int improved_any = 0;
  if (ship_cap <= 0.0) ship_cap = 1e300;

  GRBenv *env = NULL;
  if (GRBloadenv(&env, NULL) != 0) return 0;
  GRBsetintparam(env, "OutputFlag", 0);
  GRBsetintparam(env, "LogToConsole", 0);

  for (int it = 0; it < max_iters; it++) {
    Visit *visits = *visits_io;
    int n_visits = *n_visits_io;
    int port_count = 0;
    for (int i = 0; i < n_visits; i++) if (visits[i].type == tPORT) port_count++;
    if (port_count == 0) break;

    int *port_pos = (int*)xmalloc((size_t)port_count * sizeof(int));
    int p = 0;
    for (int i = 0; i < n_visits; i++) {
      if (visits[i].type == tPORT) port_pos[p++] = i;
    }

    int improved = 0;
    for (int b = 0; b < port_count; b++) {
      int boundary_idx = port_pos[b];
      int prev_port_idx = (b == 0) ? -1 : port_pos[b - 1];
      int next_port_idx = (b + 1 < port_count) ? port_pos[b + 1] : -1;
      int start_idx = prev_port_idx + 1;
      int end_idx_excl = (next_port_idx >= 0) ? (next_port_idx + 1) : n_visits;

      int n_total = 0;
      int k0 = 0;
      for (int i = start_idx; i < end_idx_excl; i++) {
        if (visits[i].type == tSTAT) {
          if (i < boundary_idx) k0++;
          n_total++;
        }
      }
      if (n_total < 2 || k0 <= 0 || k0 >= n_total) continue;

      int *stations = (int*)xmalloc((size_t)n_total * sizeof(int));
      int pos = 0;
      for (int i = start_idx; i < end_idx_excl; i++) {
        if (visits[i].type != tSTAT) continue;
        stations[pos++] = visits[i].ex_idx;
      }

      double *prefix = (double*)xmalloc((size_t)(n_total + 1) * sizeof(double));
      prefix[0] = 0.0;
      for (int i = 0; i < n_total; i++) {
        prefix[i + 1] = prefix[i] + ex->Amount[stations[i]];
      }

      double start_rad[2];
      double port_rad[2];
      double end_rad[2];
      if (prev_port_idx < 0) {
        start_rad[0] = ex->LatLonRad[0];
        start_rad[1] = ex->LatLonRad[1];
      } else {
        int prev_ex = visits[prev_port_idx].ex_idx;
        start_rad[0] = ex->LatLonRad[prev_ex * 4 + 0];
        start_rad[1] = ex->LatLonRad[prev_ex * 4 + 1];
      }
      int port_ex = visits[boundary_idx].ex_idx;
      port_rad[0] = ex->LatLonRad[port_ex * 4 + 0];
      port_rad[1] = ex->LatLonRad[port_ex * 4 + 1];
      if (next_port_idx < 0) {
        end_rad[0] = ex->LatLonRad[2];
        end_rad[1] = ex->LatLonRad[3];
      } else {
        int next_ex = visits[next_port_idx].ex_idx;
        end_rad[0] = ex->LatLonRad[next_ex * 4 + 0];
        end_rad[1] = ex->LatLonRad[next_ex * 4 + 1];
      }

      double current_sum = 1e300;
      {
        double d0 = 0.0, d1 = 0.0;
        if (solve_segment_distance(env, ex, stations, k0, start_rad, port_rad,
                                   timelimit, &d0, NULL, NULL) == 0 &&
            solve_segment_distance(env, ex, stations + k0, n_total - k0, port_rad, end_rad,
                                   timelimit, &d1, NULL, NULL) == 0 &&
            d0 >= 0.0 && d1 >= 0.0) {
          current_sum = d0 + d1;
        }
      }

      double best_sum = current_sum;
      int best_k = k0;
      for (int k = 1; k < n_total; k++) {
        double load_left = prefix[k];
        double load_right = prefix[n_total] - prefix[k];
        if (load_left > ship_cap || load_right > ship_cap) continue;
        double d0 = 0.0, d1 = 0.0;
        if (solve_segment_distance(env, ex, stations, k, start_rad, port_rad,
                                   timelimit, &d0, NULL, NULL) != 0) continue;
        if (solve_segment_distance(env, ex, stations + k, n_total - k, port_rad, end_rad,
                                   timelimit, &d1, NULL, NULL) != 0) continue;
        if (d0 < 0.0 || d1 < 0.0) continue;
        double sum = d0 + d1;
        if (sum + 1e-9 < best_sum) {
          best_sum = sum;
          best_k = k;
        }
      }

      if (best_k != k0 && best_sum + 1e-9 < current_sum) {
        int has_next_port = (next_port_idx >= 0);
        int slice_len = end_idx_excl - start_idx;
        int expected_len = n_total + 1 + (has_next_port ? 1 : 0);
        if (slice_len == expected_len) {
          Visit *tmp = (Visit*)xmalloc((size_t)slice_len * sizeof(Visit));
          int out = 0;
          for (int i = 0; i < best_k; i++) {
            tmp[out].type = tSTAT;
            tmp[out].ex_idx = stations[i];
            out++;
          }
          tmp[out].type = tPORT;
          tmp[out].ex_idx = port_ex;
          out++;
          for (int i = best_k; i < n_total; i++) {
            tmp[out].type = tSTAT;
            tmp[out].ex_idx = stations[i];
            out++;
          }
          if (has_next_port) {
            tmp[out].type = tPORT;
            tmp[out].ex_idx = visits[next_port_idx].ex_idx;
            out++;
          }
          memcpy(&visits[start_idx], tmp, (size_t)slice_len * sizeof(Visit));
          free(tmp);
          printf("Boundary opt iter %d: PORT-%d split %d->%d (%.3f -> %.3f)\n",
                 it + 1, port_ex, k0, best_k, current_sum, best_sum);
          improved = 1;
        }
      }

      free(prefix);
      free(stations);
    }

    free(port_pos);
    if (!improved) {
      printf("Boundary opt iter %d: no improvement\n", it + 1);
      break;
    }
    improved_any = 1;
  }

  GRBfreeenv(env);
  return improved_any;
}

static int merge_segments_greedy(Visit **visits_io, int *n_visits_io,
                                 const ExData *ex, double ship_cap,
                                 double timelimit) {
  if (!visits_io || !*visits_io || !n_visits_io || *n_visits_io <= 0) return 0;
  if (ship_cap <= 0.0) ship_cap = 1e300;

  GRBenv *env = NULL;
  if (GRBloadenv(&env, NULL) != 0) return 0;
  GRBsetintparam(env, "OutputFlag", 0);
  GRBsetintparam(env, "LogToConsole", 0);

  int merged_any = 0;
  for (;;) {
    Visit *visits = *visits_io;
    int n_visits = *n_visits_io;
    int port_count = 0;
    for (int i = 0; i < n_visits; i++) if (visits[i].type == tPORT) port_count++;
    if (port_count == 0) break;

    int *port_pos = (int*)xmalloc((size_t)port_count * sizeof(int));
    int p = 0;
    for (int i = 0; i < n_visits; i++) {
      if (visits[i].type == tPORT) port_pos[p++] = i;
    }

    int merged_this_pass = 0;
    for (int b = 0; b < port_count; b++) {
      int boundary_idx = port_pos[b];
      int prev_port_idx = (b == 0) ? -1 : port_pos[b - 1];
      int next_port_idx = (b + 1 < port_count) ? port_pos[b + 1] : -1;
      int start_idx = prev_port_idx + 1;
      int end_idx_excl = (next_port_idx >= 0) ? (next_port_idx + 1) : n_visits;

      int n_total = 0;
      int k0 = 0;
      for (int i = start_idx; i < end_idx_excl; i++) {
        if (visits[i].type == tSTAT) {
          if (i < boundary_idx) k0++;
          n_total++;
        }
      }
      if (n_total == 0) continue;

      int *stations = (int*)xmalloc((size_t)n_total * sizeof(int));
      int pos = 0;
      for (int i = start_idx; i < end_idx_excl; i++) {
        if (visits[i].type != tSTAT) continue;
        stations[pos++] = visits[i].ex_idx;
      }

      double *prefix = (double*)xmalloc((size_t)(n_total + 1) * sizeof(double));
      prefix[0] = 0.0;
      for (int i = 0; i < n_total; i++) {
        prefix[i + 1] = prefix[i] + ex->Amount[stations[i]];
      }
      double load_left = prefix[k0];
      double load_right = prefix[n_total] - prefix[k0];
      double load_total = prefix[n_total];
      int can_merge = (load_total <= ship_cap);

      double start_rad[2];
      double port_rad[2];
      double end_rad[2];
      if (prev_port_idx < 0) {
        start_rad[0] = ex->LatLonRad[0];
        start_rad[1] = ex->LatLonRad[1];
      } else {
        int prev_ex = visits[prev_port_idx].ex_idx;
        start_rad[0] = ex->LatLonRad[prev_ex * 4 + 0];
        start_rad[1] = ex->LatLonRad[prev_ex * 4 + 1];
      }
      int port_ex = visits[boundary_idx].ex_idx;
      port_rad[0] = ex->LatLonRad[port_ex * 4 + 0];
      port_rad[1] = ex->LatLonRad[port_ex * 4 + 1];
      if (next_port_idx < 0) {
        end_rad[0] = ex->LatLonRad[2];
        end_rad[1] = ex->LatLonRad[3];
      } else {
        int next_ex = visits[next_port_idx].ex_idx;
        end_rad[0] = ex->LatLonRad[next_ex * 4 + 0];
        end_rad[1] = ex->LatLonRad[next_ex * 4 + 1];
      }

      int do_merge = 0;
      double sep_sum = 1e300;
      double merged_dist = 1e300;
      if (can_merge) {
        if (k0 == 0 || k0 == n_total) {
          do_merge = 1;
        } else {
          double d0 = 0.0, d1 = 0.0, dmerge = 0.0;
          if (solve_segment_distance(env, ex, stations, k0, start_rad, port_rad,
                                     timelimit, &d0, NULL, NULL) == 0 &&
              solve_segment_distance(env, ex, stations + k0, n_total - k0, port_rad, end_rad,
                                     timelimit, &d1, NULL, NULL) == 0 &&
              solve_segment_distance(env, ex, stations, n_total, start_rad, end_rad,
                                     timelimit, &dmerge, NULL, NULL) == 0 &&
              d0 >= 0.0 && d1 >= 0.0 && dmerge >= 0.0) {
            sep_sum = d0 + d1;
            merged_dist = dmerge;
            if (dmerge + 1e-9 < sep_sum) do_merge = 1;
          }
        }
      }

      if (do_merge) {
        memmove(&visits[boundary_idx], &visits[boundary_idx + 1],
                (size_t)(n_visits - boundary_idx - 1) * sizeof(Visit));
        (*n_visits_io)--;
        if (merged_dist < 1e299) {
          printf("Merge pass: removed PORT-%d (%.3f -> %.3f, load=%.0f)\n",
                 port_ex, sep_sum, merged_dist, load_total);
        } else {
          printf("Merge pass: removed PORT-%d (load=%.0f)\n", port_ex, load_total);
        }
        merged_any = 1;
        merged_this_pass = 1;
      }

      free(prefix);
      free(stations);

      if (merged_this_pass) break;
    }

    free(port_pos);
    if (!merged_this_pass) break;
  }

  GRBfreeenv(env);
  return merged_any;
}

static int *build_tour_from_eval(const SegmentEval *evals, int nseg, int *out_len) {
  int cap = 1;
  for (int i = 0; i < nseg; i++) cap += evals[i].order_len + 1;
  int *tour = (int*)xmalloc((size_t)cap * sizeof(int));
  int len = 0;
  tour[len++] = 0;
  for (int i = 0; i < nseg; i++) {
    for (int k = 0; k < evals[i].order_len; k++) tour[len++] = evals[i].order[k];
    if (evals[i].res.end_type == tPORT) tour[len++] = evals[i].res.end_ex;
  }
  *out_len = len;
  return tour;
}

static void move_visit(Visit *visits, int n_visits, int from_idx, int to_idx) {
  if (from_idx == to_idx) return;
  Visit v = visits[from_idx];
  if (from_idx < to_idx) {
    memmove(&visits[from_idx], &visits[from_idx + 1],
            (size_t)(to_idx - from_idx) * sizeof(Visit));
    visits[to_idx] = v;
  } else {
    memmove(&visits[to_idx + 1], &visits[to_idx],
            (size_t)(from_idx - to_idx) * sizeof(Visit));
    visits[to_idx] = v;
  }
}

struct callback_data {
  int n;
  int numvars;
};

typedef struct {
  int status;
  int solcount;
  double obj;
  double bound;
  double gap;
  double runtime;
  double nodecnt;
} CapSolveStats;
static void findsubtour_directed(int n, double *sol, int *tourlenP, int *tour);
int __stdcall subtourelim(GRBmodel *model, void *cbdata, int where, void *usrdata);
static int *node_tour_to_letour(const int *tour, int len, int Size, int *out_len);
static void free_exdata(ExData *ex);
static int count_segments(const Visit *visits, int n_visits);
static void build_waypoint_dist(const ExData *ex,
                                const double *Land, int nLand,
                                double **out_dist, int **out_fsb,
                                double **out_full_dist, int **out_full_fsb,
                                int *out_full_m);

static double min_pair_dist(const double *dist, int n, int a, int b) {
  int a0 = 2 * a;
  int a1 = 2 * a + 1;
  int b0 = 2 * b;
  int b1 = 2 * b + 1;
  double d = dist[a0*n + b0];
  if (dist[a0*n + b1] < d) d = dist[a0*n + b1];
  if (dist[a1*n + b0] < d) d = dist[a1*n + b0];
  if (dist[a1*n + b1] < d) d = dist[a1*n + b1];
  if (dist[b0*n + a0] < d) d = dist[b0*n + a0];
  if (dist[b0*n + a1] < d) d = dist[b0*n + a1];
  if (dist[b1*n + a0] < d) d = dist[b1*n + a0];
  if (dist[b1*n + a1] < d) d = dist[b1*n + a1];
  return d;
}

static void init_cap_stats(CapSolveStats *st) {
  if (!st) return;
  st->status = 0;
  st->solcount = 0;
  st->obj = NAN;
  st->bound = NAN;
  st->gap = NAN;
  st->runtime = 0.0;
  st->nodecnt = 0.0;
}

static int solve_capacity_tour(const ExData *seg, const double *dist,
                               double ship_cap, double timelimit, int verbose,
                               int **out_tour, int *out_len, double *out_obj,
                               CapSolveStats *out_stats) {
  (void)verbose;
  int n = 2 * seg->Size;
  GRBenv *env = NULL;
  GRBmodel *model = NULL;
  int error = 0;
  init_cap_stats(out_stats);

  if (GRBloadenv(&env, NULL) != 0) return 0;
  error = GRBnewmodel(env, &model, "cap_segment", 0, NULL, NULL, NULL, NULL, NULL);
  if (error) goto QUIT;

  if (timelimit > 0.0) {
    GRBsetdblparam(GRBgetenv(model), "TimeLimit", timelimit);
  }
  GRBsetintparam(GRBgetenv(model), "Threads", 4);
  GRBsetintparam(GRBgetenv(model), "OutputFlag", 1);
  GRBsetintparam(GRBgetenv(model), "LogToConsole", 1);

  for (int i = 0; i < n; i++) {
    for (int j = 0; j < n; j++) {
      char vname[64];
      sprintf(vname, "e_%d_%d", i, j);
      error = GRBaddvar(model, 0, NULL, NULL, dist[i*n + j], 0.0, 1.0, GRB_BINARY, vname);
      if (error) goto QUIT;
    }
  }

  if (ship_cap <= 0.0) ship_cap = 1e300;
  int xcount = n * n;
  int w_offset = xcount;
  int v_offset = xcount + xcount;

  for (int i = 0; i < n; i++) {
    for (int j = 0; j < n; j++) {
      char vname[64];
      sprintf(vname, "w_%d_%d", i, j);
      error = GRBaddvar(model, 0, NULL, NULL, 0.0, 0.0, ship_cap, GRB_CONTINUOUS, vname);
      if (error) goto QUIT;
    }
  }
  for (int i = 0; i < n; i++) {
    for (int j = 0; j < n; j++) {
      char vname[64];
      int k = i / 2;
      int allow_v = (i != j) && (i / 2 == j / 2) && (seg->Type[k] != tPORT);
      double ub = allow_v ? ship_cap : 0.0;
      sprintf(vname, "v_%d_%d", i, j);
      error = GRBaddvar(model, 0, NULL, NULL, 0.0, 0.0, ub, GRB_CONTINUOUS, vname);
      if (error) goto QUIT;
    }
  }

  int *ind = (int*)xmalloc((size_t)n * sizeof(int));
  double *val = (double*)xmalloc((size_t)n * sizeof(double));
  for (int j = 0; j < n; j++) val[j] = 1.0;

  for (int i = 0; i < n; i++) {
    for (int j = 0; j < n; j++) ind[j] = i*n + j;
    error = GRBaddconstr(model, n, ind, val, GRB_EQUAL, 1.0, NULL);
    if (error) goto QUIT;
  }
  for (int j = 0; j < n; j++) {
    for (int i = 0; i < n; i++) ind[i] = i*n + j;
    error = GRBaddconstr(model, n, ind, val, GRB_EQUAL, 1.0, NULL);
    if (error) goto QUIT;
  }

  for (int i = 0; i < seg->Size; i++) {
    int a = 2*i, b = 2*i+1;
    ind[0] = a*n + b; val[0] = 1.0;
    ind[1] = b*n + a; val[1] = 1.0;
    error = GRBaddconstr(model, 2, ind, val, GRB_EQUAL, 1.0, NULL);
    if (error) goto QUIT;
  }
  free(ind);
  free(val);

  for (int i = 0; i < n; i++) {
    error = GRBsetdblattrelement(model, GRB_DBL_ATTR_UB, i*n + i, 0.0);
    if (error) goto QUIT;
  }

  int *ind2 = (int*)xmalloc((size_t)(n + 4) * sizeof(int));
  double *val2 = (double*)xmalloc((size_t)(n + 4) * sizeof(double));
  int n2 = n / 2;
  for (int j = 0; j < n2; j++) {
    if (seg->Type[j] == tPORT) continue;
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
    error = GRBaddconstr(model, 2, ind2, val2, GRB_EQUAL, seg->Amount[j], NULL);
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
      ind2[1] = i*n + j; val2[1] = -ship_cap;
      error = GRBaddconstr(model, 2, ind2, val2, GRB_LESS_EQUAL, 0.0, NULL);
      if (error) goto QUIT;
    }
  }

  for (int j = 0; j < n2; j++) {
    if (seg->Type[j] == tPORT) continue;
    int a = 2*j, b = 2*j+1;
    ind2[0] = v_offset + a*n + b; val2[0] = 1.0;
    ind2[1] = a*n + b; val2[1] = -ship_cap;
    error = GRBaddconstr(model, 2, ind2, val2, GRB_LESS_EQUAL, 0.0, NULL);
    if (error) goto QUIT;
    ind2[0] = v_offset + b*n + a; val2[0] = 1.0;
    ind2[1] = b*n + a; val2[1] = -ship_cap;
    error = GRBaddconstr(model, 2, ind2, val2, GRB_LESS_EQUAL, 0.0, NULL);
    if (error) goto QUIT;
  }

  for (int i = 0; i < n; i++) {
    if (seg->Type[i/2] != tPORT) continue;
    for (int j = 0; j < n; j++) {
      ind2[0] = w_offset + i*n + j; val2[0] = 1.0;
      error = GRBaddconstr(model, 1, ind2, val2, GRB_EQUAL, 0.0, NULL);
      if (error) goto QUIT;
    }
  }

  free(ind2);
  free(val2);

  struct callback_data cb;
  cb.n = n;
  cb.numvars = 3 * n * n;
  error = GRBsetcallbackfunc(model, subtourelim, (void*)&cb);
  if (error) goto QUIT;
  error = GRBsetintparam(GRBgetenv(model), GRB_INT_PAR_LAZYCONSTRAINTS, 1);
  if (error) goto QUIT;

  error = GRBoptimize(model);
  if (error) goto QUIT;

  int status = 0;
  GRBgetintattr(model, GRB_INT_ATTR_STATUS, &status);
  if (out_stats) out_stats->status = status;

  if (out_stats) {
    double val = 0.0;
    if (GRBgetdblattr(model, GRB_DBL_ATTR_RUNTIME, &val) == 0) out_stats->runtime = val;
    if (GRBgetdblattr(model, GRB_DBL_ATTR_NODECOUNT, &val) == 0) out_stats->nodecnt = val;
    if (GRBgetdblattr(model, GRB_DBL_ATTR_OBJBOUND, &val) == 0) out_stats->bound = val;
    if (GRBgetdblattr(model, GRB_DBL_ATTR_MIPGAP, &val) == 0) out_stats->gap = val;
  }

  if (!(status == GRB_OPTIMAL || status == GRB_TIME_LIMIT || status == GRB_SUBOPTIMAL)) {
    error = 1;
    goto QUIT;
  }

  int solcount = 0;
  error = GRBgetintattr(model, GRB_INT_ATTR_SOLCOUNT, &solcount);
  if (error || solcount == 0) { error = 1; goto QUIT; }
  if (out_stats) out_stats->solcount = solcount;

  error = GRBgetdblattr(model, GRB_DBL_ATTR_OBJVAL, out_obj);
  if (error) goto QUIT;
  if (out_stats) out_stats->obj = *out_obj;

  double *sol = (double*)xmalloc((size_t)n*n*sizeof(double));
  error = GRBgetdblattrarray(model, GRB_DBL_ATTR_X, 0, n*n, sol);
  if (error) { free(sol); goto QUIT; }

  int *tour = (int*)xmalloc((size_t)n*sizeof(int));
  int len = 0;
  findsubtour_directed(n, sol, &len, tour);
  free(sol);

  *out_tour = tour;
  *out_len = len;
  error = 0;

QUIT:
  if (model) GRBfreemodel(model);
  if (env) GRBfreeenv(env);
  return error == 0;
}

static void log_cap_csv(FILE *fp, int pass, int boundary, int port_ex,
                        int left_count, int right_count,
                        double load_left, double load_right, int n_nodes,
                        const CapSolveStats *st, int changed) {
  if (!fp || !st) return;
  fprintf(fp,
          "%d,%d,%d,%d,%d,%.0f,%.0f,%d,%d,%d,",
          pass, boundary + 1, port_ex, left_count, right_count,
          load_left, load_right, n_nodes, st->status, st->solcount);
  if (isnan(st->obj)) fprintf(fp, "nan,");
  else fprintf(fp, "%.6f,", st->obj);
  if (isnan(st->bound)) fprintf(fp, "nan,");
  else fprintf(fp, "%.6f,", st->bound);
  if (isnan(st->gap)) fprintf(fp, "nan,");
  else fprintf(fp, "%.6f,", st->gap);
  fprintf(fp, "%.3f,%.0f,%d\n", st->runtime, st->nodecnt, changed);
  fflush(fp);
}

static int optimize_boundary_capacity(Visit **visits_io, int *n_visits_io,
                                      const ExData *ex, double ship_cap,
                                      double timelimit, int left_seg,
                                      int pass, FILE *cap_csv,
                                      GRBenv *seg_env, double seg_timelimit) {
  /* Solve a two-segment subproblem with the capacity MIP, keeping the boundary
     port fixed and reassigning stations across the boundary. */
  if (!visits_io || !*visits_io || !n_visits_io || *n_visits_io <= 0) return 0;
  if (left_seg < 0) return 0;

  Visit *visits = *visits_io;
  int n_visits = *n_visits_io;
  int port_count = 0;
  for (int i = 0; i < n_visits; i++) if (visits[i].type == tPORT) port_count++;
  if (left_seg >= port_count) return 0;

  int boundary_idx = -1;
  int seg = 0;
  for (int i = 0; i < n_visits; i++) {
    if (visits[i].type == tPORT) {
      if (seg == left_seg) { boundary_idx = i; break; }
      seg++;
    }
  }
  if (boundary_idx < 0) return 0;

  int prev_port_idx = -1;
  for (int i = boundary_idx - 1; i >= 0; i--) {
    if (visits[i].type == tPORT) { prev_port_idx = i; break; }
  }
  int next_port_idx = -1;
  for (int i = boundary_idx + 1; i < n_visits; i++) {
    if (visits[i].type == tPORT) { next_port_idx = i; break; }
  }

  int start_idx = prev_port_idx + 1;
  int end_idx_excl = (next_port_idx >= 0) ? next_port_idx : n_visits;
  int n_total = 0;
  int k0 = 0;
  for (int i = start_idx; i < end_idx_excl; i++) {
    if (visits[i].type == tSTAT) {
      n_total++;
      if (i < boundary_idx) k0++;
    }
  }
  if (n_total < 2) return 0;

  int *stations = (int*)xmalloc((size_t)n_total * sizeof(int));
  int pos = 0;
  for (int i = start_idx; i < end_idx_excl; i++) {
    if (visits[i].type == tSTAT) stations[pos++] = visits[i].ex_idx;
  }
  if (k0 < 1 || k0 >= n_total) {
    free(stations);
    return 0;
  }

  /* Debug: show the two segments and their loads before solving. */
  double load_left = 0.0;
  double load_right = 0.0;
  for (int i = 0; i < k0; i++) load_left += ex->Amount[stations[i]];
  for (int i = k0; i < n_total; i++) load_right += ex->Amount[stations[i]];
  const char *start_label = (prev_port_idx < 0) ? "BOAT" : "PORT";
  const char *end_label = (next_port_idx < 0) ? "BOAT-END" : "PORT";
  if (prev_port_idx < 0) {
    printf("Cap boundary debug: %s -> [", start_label);
  } else {
    printf("Cap boundary debug: %s-%d -> [", start_label, visits[prev_port_idx].ex_idx);
  }
  for (int i = 0; i < k0; i++) {
    printf("%d%s", stations[i], (i + 1 == k0) ? "" : " ");
  }
  printf("] -> PORT-%d -> [", visits[boundary_idx].ex_idx);
  for (int i = k0; i < n_total; i++) {
    printf("%d%s", stations[i], (i + 1 == n_total) ? "" : " ");
  }
  if (next_port_idx < 0) {
    printf("] -> %s", end_label);
  } else {
    printf("] -> %s-%d", end_label, visits[next_port_idx].ex_idx);
  }
  printf(" | load_left=%.0f load_right=%.0f cap=%.0f\n",
         load_left, load_right, ship_cap);
  double total_load = 0.0;
  for (int i = 0; i < n_total; i++) total_load += ex->Amount[stations[i]];
  if (ship_cap > 0.0 && total_load > 2.0 * ship_cap) {
    printf("Cap boundary skip: load %.0f exceeds 2x ship cap %.0f\n",
           total_load, 2.0 * ship_cap);
    CapSolveStats st;
    init_cap_stats(&st);
    st.status = -2;
    log_cap_csv(cap_csv, pass, left_seg, visits[boundary_idx].ex_idx,
                k0, n_total - k0, load_left, load_right, 0, &st, 0);
    free(stations);
    return 0;
  }

  int n_wayp = ex->SelectedSize - ex->Size;
  ExData segex;
  segex.Size = 1 + n_total + 1;
  segex.SelectedSize = segex.Size + n_wayp;
  segex.Type = (int*)xmalloc((size_t)segex.SelectedSize * sizeof(int));
  segex.ItemIndex = (int*)xmalloc((size_t)segex.SelectedSize * sizeof(int));
  segex.Amount = (double*)xmalloc((size_t)segex.SelectedSize * sizeof(double));
  segex.LatLonRad = (double*)xmalloc((size_t)segex.SelectedSize * 4 * sizeof(double));
  segex.LatLonDegMin = (double*)xmalloc((size_t)segex.SelectedSize * 4 * sizeof(double));

  int *local_to_ex = (int*)xmalloc((size_t)segex.Size * sizeof(int));

  double start_rad[2];
  double end_rad[2];
  if (prev_port_idx < 0) {
    start_rad[0] = ex->LatLonRad[0];
    start_rad[1] = ex->LatLonRad[1];
  } else {
    int prev_ex = visits[prev_port_idx].ex_idx;
    start_rad[0] = ex->LatLonRad[prev_ex * 4 + 0];
    start_rad[1] = ex->LatLonRad[prev_ex * 4 + 1];
  }
  if (next_port_idx < 0) {
    end_rad[0] = ex->LatLonRad[2];
    end_rad[1] = ex->LatLonRad[3];
  } else {
    int next_ex = visits[next_port_idx].ex_idx;
    end_rad[0] = ex->LatLonRad[next_ex * 4 + 0];
    end_rad[1] = ex->LatLonRad[next_ex * 4 + 1];
  }

  /* In the two-segment subproblem the "ship" node carries both start and end
     coordinates, so mark it as a port reset to allow capacity to reset at
     both BOAT-START and BOAT-END. */
  segex.Type[0] = tPORT;
  segex.ItemIndex[0] = -1;
  segex.Amount[0] = 0.0;
  segex.LatLonRad[0] = start_rad[0];
  segex.LatLonRad[1] = start_rad[1];
  segex.LatLonRad[2] = end_rad[0];
  segex.LatLonRad[3] = end_rad[1];
  for (int t = 0; t < 4; t++) segex.LatLonDegMin[t] = 0.0;
  local_to_ex[0] = 0;

  int k = 1;
  for (int i = 0; i < n_total; i++, k++) {
    int ex_idx = stations[i];
    segex.Type[k] = tSTAT;
    segex.ItemIndex[k] = ex->ItemIndex[ex_idx];
    segex.Amount[k] = ex->Amount[ex_idx];
    for (int t = 0; t < 4; t++) {
      segex.LatLonRad[k*4 + t] = ex->LatLonRad[ex_idx*4 + t];
      segex.LatLonDegMin[k*4 + t] = ex->LatLonDegMin[ex_idx*4 + t];
    }
    local_to_ex[k] = ex_idx;
  }

  int port_ex = visits[boundary_idx].ex_idx;
  double port_rad[2];
  port_rad[0] = ex->LatLonRad[port_ex * 4 + 0];
  port_rad[1] = ex->LatLonRad[port_ex * 4 + 1];
  segex.Type[k] = tPORT;
  segex.ItemIndex[k] = ex->ItemIndex[port_ex];
  segex.Amount[k] = 0.0;
  for (int t = 0; t < 4; t++) {
    segex.LatLonRad[k*4 + t] = ex->LatLonRad[port_ex*4 + t];
    segex.LatLonDegMin[k*4 + t] = ex->LatLonDegMin[port_ex*4 + t];
  }
  local_to_ex[k] = port_ex;
  k++;

  for (int w = 0; w < n_wayp; w++, k++) {
    int src = ex->Size + w;
    segex.Type[k] = tWAYP;
    segex.ItemIndex[k] = ex->ItemIndex[src];
    segex.Amount[k] = 0.0;
    for (int t = 0; t < 4; t++) {
      segex.LatLonRad[k*4 + t] = ex->LatLonRad[src*4 + t];
      segex.LatLonDegMin[k*4 + t] = ex->LatLonDegMin[src*4 + t];
    }
  }

  double *dist = NULL;
  int *fsb = NULL;
  build_waypoint_dist(&segex, NULL, 0, &dist, &fsb, NULL, NULL, NULL);
  if (!dist || !fsb) {
    free(dist); free(fsb); free_exdata(&segex); free(local_to_ex); free(stations);
    return 0;
  }
  int n_nodes = 2 * segex.Size;
  dist[0*n_nodes + 1] = 0.0;
  dist[1*n_nodes + 0] = 0.0;
  fsb[0*n_nodes + 1] = 1;
  fsb[1*n_nodes + 0] = 1;

  int *node_tour = NULL;
  int node_len = 0;
  double obj = -1.0;
  CapSolveStats st;

  double baseline = 1e300;
  if (seg_env) {
    double left_dist = 0.0;
    double right_dist = 0.0;
    int err_left = solve_segment_distance(seg_env, ex, stations, k0,
                                          start_rad, port_rad, seg_timelimit,
                                          &left_dist, NULL, NULL);
    int err_right = solve_segment_distance(seg_env, ex, stations + k0, n_total - k0,
                                           port_rad, end_rad, seg_timelimit,
                                           &right_dist, NULL, NULL);
    if (!err_left && !err_right && left_dist < 100000.0 && right_dist < 100000.0) {
      baseline = left_dist + right_dist;
    } else {
      printf("Cap boundary baseline infeasible: PORT-%d left=%.3f right=%.3f\n",
             port_ex, left_dist, right_dist);
    }
  }

  int ok = solve_capacity_tour(&segex, dist, ship_cap, timelimit, 1,
                               &node_tour, &node_len, &obj, &st);
  if (!ok || !node_tour || node_len <= 0) {
    log_cap_csv(cap_csv, pass, left_seg, visits[boundary_idx].ex_idx,
                k0, n_total - k0, load_left, load_right, n_nodes, &st, 0);
    free(node_tour);
    free(dist); free(fsb);
    free_exdata(&segex);
    free(local_to_ex);
    free(stations);
    return 0;
  }

  int letour_len = 0;
  int *letour = node_tour_to_letour(node_tour, node_len, segex.Size, &letour_len);
  if (!letour || letour_len <= 1) {
    log_cap_csv(cap_csv, pass, left_seg, visits[boundary_idx].ex_idx,
                k0, n_total - k0, load_left, load_right, n_nodes, &st, 0);
    free(letour);
    free(node_tour);
    free(dist); free(fsb);
    free_exdata(&segex);
    free(local_to_ex);
    free(stations);
    return 0;
  }

  int slice_len = end_idx_excl - start_idx;
  Visit *slice = (Visit*)xmalloc((size_t)slice_len * sizeof(Visit));
  int slice_n = 0;
  for (int i = 1; i < letour_len; i++) {
    int loc = abs(letour[i]);
    if (loc == 0) continue;
    int ex_idx = local_to_ex[loc];
    int typ = segex.Type[loc];
    slice[slice_n].type = (typ == tPORT) ? tPORT : tSTAT;
    slice[slice_n].ex_idx = ex_idx;
    slice_n++;
  }
  if (slice_n != slice_len) {
    log_cap_csv(cap_csv, pass, left_seg, visits[boundary_idx].ex_idx,
                k0, n_total - k0, load_left, load_right, n_nodes, &st, 0);
    free(slice);
    free(letour);
    free(node_tour);
    free(dist); free(fsb);
    free_exdata(&segex);
    free(local_to_ex);
    free(stations);
    return 0;
  }

  int unchanged = 1;
  for (int i = 0; i < slice_len; i++) {
    if (visits[start_idx + i].type != slice[i].type ||
        visits[start_idx + i].ex_idx != slice[i].ex_idx) {
      unchanged = 0;
      break;
    }
  }
  if (unchanged) {
    log_cap_csv(cap_csv, pass, left_seg, visits[boundary_idx].ex_idx,
                k0, n_total - k0, load_left, load_right, n_nodes, &st, 0);
    free(slice);
    free(letour);
    free(node_tour);
    free(dist); free(fsb);
    free_exdata(&segex);
    free(local_to_ex);
    free(stations);
    return 0;
  }

  double new_total = 1e300;
  if (seg_env && baseline < 1e299) {
    int port_pos = -1;
    int *left_list = (int*)xmalloc((size_t)slice_len * sizeof(int));
    int *right_list = (int*)xmalloc((size_t)slice_len * sizeof(int));
    int left_count = 0;
    int right_count = 0;
    for (int i = 0; i < slice_len; i++) {
      if (slice[i].type == tPORT) {
        port_pos = i;
        continue;
      }
      if (port_pos < 0) left_list[left_count++] = slice[i].ex_idx;
      else right_list[right_count++] = slice[i].ex_idx;
    }
    if (port_pos >= 0) {
      double left_dist = 0.0;
      double right_dist = 0.0;
      int err_left = solve_segment_distance(seg_env, ex, left_list, left_count,
                                            start_rad, port_rad, seg_timelimit,
                                            &left_dist, NULL, NULL);
      int err_right = solve_segment_distance(seg_env, ex, right_list, right_count,
                                             port_rad, end_rad, seg_timelimit,
                                             &right_dist, NULL, NULL);
      if (!err_left && !err_right && left_dist < 100000.0 && right_dist < 100000.0) {
        new_total = left_dist + right_dist;
      } else {
        printf("Cap boundary reject: PORT-%d segment infeasible left=%.3f right=%.3f\n",
               port_ex, left_dist, right_dist);
      }
    }
    free(left_list);
    free(right_list);
  }

  if (baseline < 1e299 && new_total >= baseline - 1e-6) {
    printf("Cap boundary reject: PORT-%d real=%.3f baseline=%.3f\n",
           port_ex, new_total, baseline);
    log_cap_csv(cap_csv, pass, left_seg, visits[boundary_idx].ex_idx,
                k0, n_total - k0, load_left, load_right, n_nodes, &st, 0);
    free(slice);
    free(letour);
    free(node_tour);
    free(dist); free(fsb);
    free_exdata(&segex);
    free(local_to_ex);
    free(stations);
    return 0;
  }

  int new_count = 0;
  Visit *new_vis = (Visit*)xmalloc((size_t)(n_visits + n_total + 4) * sizeof(Visit));
  for (int i = 0; i < start_idx; i++) new_vis[new_count++] = visits[i];
  for (int i = 0; i < slice_len; i++) new_vis[new_count++] = slice[i];
  for (int i = end_idx_excl; i < n_visits; i++) new_vis[new_count++] = visits[i];

  log_cap_csv(cap_csv, pass, left_seg, visits[boundary_idx].ex_idx,
              k0, n_total - k0, load_left, load_right, n_nodes, &st, 1);
  free(slice);
  free(*visits_io);
  *visits_io = new_vis;
  *n_visits_io = new_count;

  printf("Cap boundary opt: PORT-%d stations=%d obj=%.3f real=%.3f baseline=%.3f\n",
         port_ex, n_total, obj, new_total, baseline);

  free(letour);
  free(node_tour);
  free(dist); free(fsb);
  free_exdata(&segex);
  free(local_to_ex);
  free(stations);
  return 1;
}

static int optimize_boundaries_one_pass(Visit **visits_io, int *n_visits_io,
                                        const ExData *ex, double ship_cap,
                                        double timelimit, int pass,
                                        int *dirty, int port_count,
                                        FILE *cap_csv,
                                        GRBenv *seg_env, double seg_timelimit) {
  /* Single boundary sweep pass. Updates dirty[] in-place. */
  if (!visits_io || !*visits_io || !n_visits_io || *n_visits_io <= 0) return 0;
  if (!dirty || port_count <= 0) return 0;

  int changed = 0;
  printf("Capacity boundary sweep pass %d\n", pass);
  for (int b = 0; b < port_count; b++) {
    if (!dirty[b]) continue;
    dirty[b] = 0;
    if (optimize_boundary_capacity(visits_io, n_visits_io, ex, ship_cap,
                                   timelimit, b, pass, cap_csv,
                                   seg_env, seg_timelimit)) {
      changed = 1;
      dirty[b] = 1;
      dirty[(b + port_count - 1) % port_count] = 1;
      dirty[(b + 1) % port_count] = 1;
    }
  }
  return changed;
}

static int port_adjacent_cleanup(Visit *visits, int n_visits, const ExData *ex,
                                 const double *dist, int Size, double ship_cap) {
  if (!visits || n_visits <= 0) return 0;
  if (ship_cap <= 0.0) ship_cap = 1e300;

  int port_count = 0;
  for (int i = 0; i < n_visits; i++) if (visits[i].type == tPORT) port_count++;
  if (port_count == 0) return 0;

  int *port_pos = (int*)xmalloc((size_t)port_count * sizeof(int));
  int p = 0;
  for (int i = 0; i < n_visits; i++) {
    if (visits[i].type == tPORT) port_pos[p++] = i;
  }

  double best_improve = 0.0;
  int best_from = -1;
  int best_to = -1;
  int best_port = -1;
  const char *best_dir = NULL;
  double eps = 1e-6;
  int n = 2 * Size;

  for (int b = 0; b < port_count; b++) {
    int boundary_idx = port_pos[b];
    int prev_port_idx = (b == 0) ? -1 : port_pos[b - 1];
    int next_port_idx = (b + 1 < port_count) ? port_pos[b + 1] : -1;
    int left_start = prev_port_idx + 1;
    int left_end = boundary_idx - 1;
    int right_start = boundary_idx + 1;
    int right_end = (next_port_idx >= 0) ? (next_port_idx - 1) : (n_visits - 1);

    int left_first = -1;
    int left_last = -1;
    int right_first = -1;
    int left_count = 0;
    int right_count = 0;
    double load_left = 0.0;
    double load_right = 0.0;

    for (int i = left_start; i <= left_end; i++) {
      if (i < 0 || i >= n_visits) continue;
      if (visits[i].type != tSTAT) continue;
      if (left_first < 0) left_first = i;
      left_last = i;
      left_count++;
      load_left += ex->Amount[visits[i].ex_idx];
    }
    for (int i = right_start; i <= right_end; i++) {
      if (i < 0 || i >= n_visits) continue;
      if (visits[i].type != tSTAT) continue;
      if (right_first < 0) right_first = i;
      right_count++;
      load_right += ex->Amount[visits[i].ex_idx];
    }

    if (left_count == 0 || right_count == 0) continue;

    int port_ex = visits[boundary_idx].ex_idx;
    double d_first_right = min_pair_dist(dist, n, port_ex, visits[right_first].ex_idx);
    double d_last_left = min_pair_dist(dist, n, port_ex, visits[left_last].ex_idx);

    int best_left_idx = -1;
    double best_left_dist = 1e300;
    for (int i = left_start; i <= left_end; i++) {
      if (visits[i].type != tSTAT) continue;
      double d = min_pair_dist(dist, n, port_ex, visits[i].ex_idx);
      if (d < best_left_dist) {
        best_left_dist = d;
        best_left_idx = i;
      }
    }

    int best_right_idx = -1;
    double best_right_dist = 1e300;
    for (int i = right_start; i <= right_end; i++) {
      if (visits[i].type != tSTAT) continue;
      double d = min_pair_dist(dist, n, port_ex, visits[i].ex_idx);
      if (d < best_right_dist) {
        best_right_dist = d;
        best_right_idx = i;
      }
    }

    if (best_left_idx >= 0 && left_count > 1) {
      double amt = ex->Amount[visits[best_left_idx].ex_idx];
      if (load_right + amt <= ship_cap) {
        double improve = d_first_right - best_left_dist;
        if (improve > best_improve + eps) {
          best_improve = improve;
          best_from = best_left_idx;
          best_port = port_ex;
          best_dir = "L->R";
          best_to = (best_left_idx < boundary_idx) ? boundary_idx : boundary_idx + 1;
        }
      }
    }

    if (best_right_idx >= 0 && right_count > 1) {
      double amt = ex->Amount[visits[best_right_idx].ex_idx];
      if (load_left + amt <= ship_cap) {
        double improve = d_last_left - best_right_dist;
        if (improve > best_improve + eps) {
          best_improve = improve;
          best_from = best_right_idx;
          best_port = port_ex;
          best_dir = "R->L";
          best_to = boundary_idx;
        }
      }
    }
  }

  free(port_pos);

  if (best_from >= 0 && best_to >= 0) {
    int stat_ex = visits[best_from].ex_idx;
    move_visit(visits, n_visits, best_from, best_to);
    printf("Port cleanup: moved STAT-%d %s at PORT-%d (improve=%.3f)\n",
           stat_ex, best_dir ? best_dir : "?", best_port, best_improve);
    return 1;
  }
  return 0;
}

static int count_segments(const Visit *visits, int n_visits) {
  int port_count = 0;
  for (int i = 0; i < n_visits; i++) {
    if (visits[i].type == tPORT) port_count++;
  }
  return port_count + 1;
}

static int *collect_port_ex(const ExData *ex, int *out_count) {
  int count = 0;
  for (int i = 0; i < ex->Size; i++) {
    if (ex->Type[i] == tPORT) count++;
  }
  int *ports = NULL;
  if (count > 0) ports = (int*)xmalloc((size_t)count * sizeof(int));
  int n = 0;
  for (int i = 0; i < ex->Size; i++) {
    if (ex->Type[i] == tPORT) ports[n++] = i;
  }
  *out_count = count;
  return ports;
}

static int swap_port_visit(Visit *visits, int n_visits, const ExData *ex, int *out_seg) {
  int port_visit_count = 0;
  for (int i = 0; i < n_visits; i++) {
    if (visits[i].type == tPORT) port_visit_count++;
  }
  if (port_visit_count == 0) return 0;

  int *port_vis_idx = (int*)xmalloc((size_t)port_visit_count * sizeof(int));
  int pv = 0;
  for (int i = 0; i < n_visits; i++) {
    if (visits[i].type == tPORT) port_vis_idx[pv++] = i;
  }

  int port_ex_count = 0;
  int *port_ex = collect_port_ex(ex, &port_ex_count);
  if (port_ex_count <= 1) {
    free(port_vis_idx);
    free(port_ex);
    return 0;
  }

  int pick_vis = port_vis_idx[rand() % port_visit_count];
  int current_ex = visits[pick_vis].ex_idx;

  int cand_count = 0;
  for (int i = 0; i < port_ex_count; i++) {
    if (port_ex[i] != current_ex) cand_count++;
  }
  if (cand_count == 0) {
    free(port_vis_idx);
    free(port_ex);
    return 0;
  }

  int pick = rand() % cand_count;
  int new_ex = current_ex;
  for (int i = 0; i < port_ex_count; i++) {
    if (port_ex[i] == current_ex) continue;
    if (pick == 0) { new_ex = port_ex[i]; break; }
    pick--;
  }

  visits[pick_vis].ex_idx = new_ex;
  if (out_seg) {
    int seg = 0;
    for (int i = 0; i < pick_vis; i++) {
      if (visits[i].type == tPORT) seg++;
    }
    *out_seg = seg;
  }
  printf("Hillclimb port swap: PORT-%d -> PORT-%d\n", current_ex, new_ex);

  free(port_vis_idx);
  free(port_ex);
  return 1;
}

static int merge_segments_by_capacity(Visit *visits, int *n_visits_io,
                                      const ExData *ex, double ship_cap,
                                      int *out_seg, int *out_port_ex,
                                      double *out_combined_load) {
  int n_visits = *n_visits_io;
  int port_count = 0;
  for (int i = 0; i < n_visits; i++) {
    if (visits[i].type == tPORT) port_count++;
  }
  if (port_count == 0) return 0;
  if (ship_cap <= 0.0) ship_cap = 1.0;

  int seg_count = port_count + 1;
  double *seg_amount = (double*)xcalloc((size_t)seg_count, sizeof(double));
  int *boundary_idx = (int*)xmalloc((size_t)port_count * sizeof(int));

  int seg = 0;
  for (int i = 0; i < n_visits; i++) {
    if (visits[i].type == tSTAT) {
      seg_amount[seg] += ex->Amount[visits[i].ex_idx];
    } else if (visits[i].type == tPORT) {
      boundary_idx[seg] = i;
      seg++;
    }
  }

  int *cand_idx = (int*)xmalloc((size_t)port_count * sizeof(int));
  int *cand_seg = (int*)xmalloc((size_t)port_count * sizeof(int));
  int cand_count = 0;
  for (int s = 0; s + 1 < seg_count; s++) {
    if (seg_amount[s] + seg_amount[s + 1] <= ship_cap) {
      cand_idx[cand_count] = boundary_idx[s];
      cand_seg[cand_count] = s;
      cand_count++;
    }
  }

  if (cand_count == 0) {
    free(seg_amount);
    free(boundary_idx);
    free(cand_idx);
    free(cand_seg);
    return 0;
  }

  int pick = rand() % cand_count;
  int pick_idx = cand_idx[pick];
  int pick_seg = cand_seg[pick];
  int port_ex = visits[pick_idx].ex_idx;
  memmove(&visits[pick_idx], &visits[pick_idx + 1],
          (size_t)(n_visits - pick_idx - 1) * sizeof(Visit));
  (*n_visits_io)--;
  if (out_seg) *out_seg = pick_seg;
  if (out_port_ex) *out_port_ex = port_ex;
  if (out_combined_load) *out_combined_load = seg_amount[pick_seg] + seg_amount[pick_seg + 1];

  free(seg_amount);
  free(boundary_idx);
  free(cand_idx);
  free(cand_seg);
  return 1;
}

static int move_one_station(Visit *visits, int n_visits, const ExData *ex,
                            const double *dist, int Size, double ship_cap,
                            double tau_scale, const int *allowed_src, int allowed_len,
                            int *out_src_seg, int *out_dst_seg, int *out_attempts) {
  int port_count = 0;
  for (int i = 0; i < n_visits; i++) {
    if (visits[i].type == tPORT) port_count++;
  }
  int seg_count = port_count + 1;
  int use_allowed = (allowed_src && allowed_len == seg_count);
  const double INF = 1e300;
  if (seg_count < 2) {
    printf("Hillclimb: only one segment, skipping move.\n");
    return 0;
  }

  int *seg_first = (int*)xcalloc((size_t)seg_count, sizeof(int));
  int *seg_end = (int*)xmalloc((size_t)seg_count * sizeof(int));
  int *seg_station_count = (int*)xcalloc((size_t)seg_count, sizeof(int));
  double *seg_amount = (double*)xcalloc((size_t)seg_count, sizeof(double));
  int *seg_of_idx = (int*)xmalloc((size_t)n_visits * sizeof(int));
  for (int i = 0; i < seg_count; i++) seg_end[i] = -1;

  int seg = 0;
  seg_first[0] = 0;
  for (int i = 0; i < n_visits; i++) {
    seg_of_idx[i] = seg;
    if (visits[i].type == tSTAT) {
      seg_station_count[seg]++;
      seg_amount[seg] += ex->Amount[visits[i].ex_idx];
    } else if (visits[i].type == tPORT) {
      seg_end[seg] = i;
      seg++;
      if (seg < seg_count) seg_first[seg] = i + 1;
    }
  }

  int *seg_station_offsets = (int*)xcalloc((size_t)seg_count + 1, sizeof(int));
  for (int i = 0; i < seg_count; i++) {
    seg_station_offsets[i + 1] = seg_station_offsets[i] + seg_station_count[i];
  }
  int total_stations = seg_station_offsets[seg_count];
  int *seg_station_ex = NULL;
  int *seg_station_pos = NULL;
  if (total_stations > 0) {
    seg_station_ex = (int*)xmalloc((size_t)total_stations * sizeof(int));
    seg_station_pos = (int*)xcalloc((size_t)seg_count, sizeof(int));
    for (int i = 0; i < n_visits; i++) {
      if (visits[i].type != tSTAT) continue;
      int s = seg_of_idx[i];
      int pos = seg_station_offsets[s] + seg_station_pos[s]++;
      seg_station_ex[pos] = visits[i].ex_idx;
    }
  }

  double *seg_tol = (double*)xmalloc((size_t)seg_count * sizeof(double));
  for (int s = 0; s < seg_count; s++) {
    int cnt = seg_station_count[s];
    if (cnt <= 1) {
      seg_tol[s] = INF;
      continue;
    }
    double sum = 0.0;
    int off = seg_station_offsets[s];
    for (int i = 0; i < cnt; i++) {
      double min_d = INF;
      int ex_i = seg_station_ex[off + i];
      for (int j = 0; j < cnt; j++) {
        if (i == j) continue;
        double d = min_pair_dist(dist, 2 * Size, ex_i, seg_station_ex[off + j]);
        if (d < min_d) min_d = d;
      }
      sum += min_d;
    }
    seg_tol[s] = sum / (double)cnt;
  }

  int *cand = (int*)xmalloc((size_t)seg_count * sizeof(int));
  double min_amt = 0.0;
  int max_attempts = 5;
  for (int attempt = 0; attempt < max_attempts; attempt++) {
    int cand_count = 0;
    for (int i = 0; i < seg_count; i++) {
      if (use_allowed && !allowed_src[i]) continue;
      if (seg_station_count[i] == 0) continue;
      int has = 0;
      for (int j = seg_first[i]; j < ((seg_end[i] >= 0) ? seg_end[i] : n_visits); j++) {
        if (seg_of_idx[j] != i) continue;
        if (visits[j].type != tSTAT) continue;
        if (ex->Amount[visits[j].ex_idx] >= min_amt) { has = 1; break; }
      }
      if (has) cand[cand_count++] = i;
    }
    if (cand_count == 0) {
      printf("Hillclimb: no stations to move (min_amt=%.0f).\n", min_amt);
      if (out_attempts) *out_attempts = attempt + 1;
      break;
    }

    int src_seg = cand[rand() % cand_count];

    int *dest_cand = (int*)xmalloc((size_t)seg_count * sizeof(int));
    int dest_count = 0;
    for (int j = 0; j < seg_count; j++) {
      if (j == src_seg) continue;
      double tol = seg_tol[src_seg] > seg_tol[j] ? seg_tol[src_seg] : seg_tol[j];
      int ok = 0;
      if (tol >= INF * 0.5) {
        ok = 1;
      } else {
        int cnt_src = seg_station_count[src_seg];
        int cnt_dst = seg_station_count[j];
        if (cnt_src == 0 || cnt_dst == 0) {
          ok = 1;
        } else {
          double min_cross = INF;
          int off_src = seg_station_offsets[src_seg];
          int off_dst = seg_station_offsets[j];
          for (int a = 0; a < cnt_src; a++) {
            int ex_a = seg_station_ex[off_src + a];
            for (int b = 0; b < cnt_dst; b++) {
              double d = min_pair_dist(dist, 2 * Size, ex_a, seg_station_ex[off_dst + b]);
              if (d < min_cross) min_cross = d;
              if (min_cross <= tol) break;
            }
            if (min_cross <= tol) break;
          }
          if (min_cross <= tol) ok = 1;
        }
      }
      if (ok) dest_cand[dest_count++] = j;
    }
    if (dest_count == 0) {
      free(dest_cand);
      continue;
    }

    int dst_seg = dest_cand[rand() % dest_count];
    free(dest_cand);
    if (dst_seg == src_seg) continue;
    int dir = rand() % 2; /* 0 end, 1 start */

    int dest_has = 0;
    if (seg_station_count[dst_seg] > 0) dest_has = 1;
    int dest_ex = 0;
    if (!dest_has) {
      if (seg_end[dst_seg] >= 0) dest_ex = visits[seg_end[dst_seg]].ex_idx;
      else dest_ex = 0;
    }

    int from_idx = -1;
    int max_cand = seg_station_count[src_seg];
    int *cand_idx = (int*)xmalloc((size_t)max_cand * sizeof(int));
    double *cand_d = (double*)xmalloc((size_t)max_cand * sizeof(double));
    int cand_n = 0;
    double min_d = 0.0;
    double max_d = 0.0;
    for (int i = 0; i < n_visits; i++) {
      if (seg_of_idx[i] != src_seg) continue;
      if (visits[i].type != tSTAT) continue;
      if (ex->Amount[visits[i].ex_idx] < min_amt) continue;
      double dmin = 0.0;
      if (dest_has) {
        dmin = 1e100;
        int off_dst = seg_station_offsets[dst_seg];
        int cnt_dst = seg_station_count[dst_seg];
        for (int j = 0; j < cnt_dst; j++) {
          double d = min_pair_dist(dist, 2 * Size, visits[i].ex_idx, seg_station_ex[off_dst + j]);
          if (d < dmin) dmin = d;
        }
      } else {
        dmin = min_pair_dist(dist, 2 * Size, visits[i].ex_idx, dest_ex);
      }
      cand_idx[cand_n] = i;
      cand_d[cand_n] = dmin;
      if (cand_n == 0 || dmin < min_d) min_d = dmin;
      if (cand_n == 0 || dmin > max_d) max_d = dmin;
      cand_n++;
    }
    if (cand_n == 0) {
      free(cand_idx);
      free(cand_d);
      continue;
    }

    double T = 0.5 * (min_d + max_d) * tau_scale;
    double tau = T;
    int pick = 0;
    if (!isfinite(T) || T <= 0.0) {
      double p = 1.0 / (double)cand_n;
      printf("Move sampling: min_d=%.3f nm max_d=%.3f nm T=%.3f tau=%.3f min_p=%.6f max_p=%.6f\n",
             min_d, max_d, T, tau, p, p);
      pick = rand() % cand_n;
    } else {
      double sum = 0.0;
      for (int i = 0; i < cand_n; i++) {
        sum += exp(-(cand_d[i] - min_d) / T);
      }
      double min_p = 0.0;
      double max_p = 0.0;
      for (int i = 0; i < cand_n; i++) {
        double p = exp(-(cand_d[i] - min_d) / T) / sum;
        if (i == 0 || p < min_p) min_p = p;
        if (i == 0 || p > max_p) max_p = p;
      }
      printf("Move sampling: min_d=%.3f nm max_d=%.3f nm T=%.3f tau=%.3f min_p=%.6f max_p=%.6f\n",
             min_d, max_d, T, tau, min_p, max_p);
      double r = ((double)rand() / (double)RAND_MAX) * sum;
      double acc = 0.0;
      pick = cand_n - 1; /* guard against floating-point drift */
      for (int i = 0; i < cand_n; i++) {
        acc += exp(-(cand_d[i] - min_d) / T);
        if (r <= acc) { pick = i; break; }
      }
    }
    from_idx = cand_idx[pick];
    free(cand_idx);
    free(cand_d);
    if (from_idx < 0) continue;
    Visit moved = visits[from_idx];
    double amt = ex->Amount[moved.ex_idx];

    if (seg_amount[dst_seg] + amt > ship_cap) {
      int adjacent = (abs(dst_seg - src_seg) == 1);
      double pair_load = seg_amount[src_seg] + seg_amount[dst_seg];
      if (!adjacent || pair_load > 2.0 * ship_cap) {
        if (amt > min_amt) min_amt = amt;
        continue;
      }
    }

    int insert_idx = 0;
    if (dir == 0) {
      insert_idx = (seg_end[dst_seg] >= 0) ? seg_end[dst_seg] : n_visits;
    } else {
      insert_idx = seg_first[dst_seg];
    }
    if (insert_idx < 0) insert_idx = 0;
    if (insert_idx > n_visits) insert_idx = n_visits;

    if (from_idx < insert_idx) {
      int shift = insert_idx - from_idx - 1;
      if (shift > 0) {
        memmove(&visits[from_idx], &visits[from_idx + 1], (size_t)shift * sizeof(Visit));
      }
      insert_idx--;
    } else if (from_idx > insert_idx) {
      int shift = from_idx - insert_idx;
      if (shift > 0) {
        memmove(&visits[insert_idx + 1], &visits[insert_idx], (size_t)shift * sizeof(Visit));
      }
    }
    visits[insert_idx] = moved;

    printf("Hillclimb move: STAT-%d (amt=%.0f) seg %d -> seg %d (%s)\n",
           moved.ex_idx, amt, src_seg + 1, dst_seg + 1, dir ? "start" : "end");
    if (out_src_seg) *out_src_seg = src_seg;
    if (out_dst_seg) *out_dst_seg = dst_seg;
    if (out_attempts) *out_attempts = attempt + 1;

    free(seg_first); free(seg_end); free(seg_station_count);
    free(seg_amount); free(seg_of_idx); free(cand);
    free(seg_station_offsets); free(seg_station_ex);
    free(seg_station_pos); free(seg_tol);
    return 1;
  }

  if (out_attempts) *out_attempts = max_attempts;
  printf("Hillclimb: no feasible move after %d attempts.\n", max_attempts);
  free(seg_first); free(seg_end); free(seg_station_count);
  free(seg_amount); free(seg_of_idx); free(cand);
  free(seg_station_offsets); free(seg_station_ex);
  free(seg_station_pos); free(seg_tol);
  return 0;
}

static void print_segment_stats_forward(const int *letour, int letour_len,
                                        const ExData *ex, const ItemVec *items,
                                        const double *dist) {
  if (!letour || letour_len <= 0) return;
  int n = 2 * ex->Size;
  int prev_node = 0;
  const char *boat_name = "BOAT";
  if (items->a[ex->ItemIndex[0]].Name) boat_name = items->a[ex->ItemIndex[0]].Name;
  const char *start_label = boat_name;
  int stations = 0;
  double amount = 0.0;
  double dist_seg = 0.0;
  int seg = 1;

  for (int i = 1; i < letour_len; i++) {
    int idx = letour[i];
    if (idx == 0) continue;
    int k = abs(idx);
    int entry = 2 * k + (idx < 0 ? 1 : 0);
    int exit_node = 2 * k + (idx < 0 ? 0 : 1);
    dist_seg += dist[prev_node * n + entry];
    dist_seg += dist[entry * n + exit_node];
    prev_node = exit_node;

    if (ex->Type[k] == tSTAT) {
      stations += 1;
      amount += ex->Amount[k];
    }

    if (ex->Type[k] == tPORT) {
      const char *end_label = "PORT";
      Item *pt = &items->a[ex->ItemIndex[k]];
      if (pt->Name) end_label = pt->Name;
      printf("Segment %d: %s -> %s | stations=%d distance=%.3f amount=%.0f\n",
             seg, start_label, end_label, stations, dist_seg, amount);
      seg++;
      start_label = end_label;
      stations = 0;
      amount = 0.0;
      dist_seg = 0.0;
    }
  }
  dist_seg += dist[prev_node * n + 1];
  printf("Segment %d: %s -> %s-END | stations=%d distance=%.3f amount=%.0f\n",
         seg, start_label, boat_name, stations, dist_seg, amount);
}

static int write_plot_bundle_letour(const char *fname,
                                    int Size,
                                    int SelectedSize,
                                    const int *letour,
                                    int letour_len,
                                    const int *Type,
                                    const double *Amount,
                                    const double *LatLonRad,
                                    const ItemVec *items,
                                    const ExData *ex)
{
  FILE *fp = fopen(fname, "w");
  if (!fp) { perror("fopen(write_plot_bundle_letour)"); return 1; }

  fprintf(fp, "PLOT_BUNDLE_V1\n");
  fprintf(fp, "Size %d\n", Size);
  fprintf(fp, "SelectedSize %d\n", SelectedSize);

  fprintf(fp, "Tour");
  for (int i = 0; i < letour_len; i++) fprintf(fp, " %d", letour[i]);
  fprintf(fp, "\n");

  fprintf(fp, "Type");
  for (int i = 0; i < SelectedSize; i++) fprintf(fp, " %d", Type[i]);
  fprintf(fp, "\n");

  fprintf(fp, "Amount");
  for (int i = 0; i < SelectedSize; i++) fprintf(fp, " %.17g", Amount[i]);
  fprintf(fp, "\n");

  fprintf(fp, "Name\n");
  for (int i = 0; i < SelectedSize; i++) {
    const char *name = NULL;
    if (ex && ex->ItemIndex && i < ex->SelectedSize) {
      int item_idx = ex->ItemIndex[i];
      if (item_idx >= 0 && item_idx < items->n) {
        name = items->a[item_idx].Name;
      }
    }
    write_name_line(fp, name);
    fputc('\n', fp);
  }

  fprintf(fp, "LatLonRad\n");
  for (int i = 0; i < SelectedSize; i++) {
    fprintf(fp, "%.17g %.17g %.17g %.17g\n",
            LatLonRad[i*4+0], LatLonRad[i*4+1], LatLonRad[i*4+2], LatLonRad[i*4+3]);
  }

  fclose(fp);
  return 0;
}

static void free_exdata(ExData *ex){
  free(ex->Type);
  free(ex->ItemIndex);
  free(ex->Amount);
  free(ex->LatLonRad);
  free(ex->LatLonDegMin);
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

static ExData build_exdata_no_ports(const ItemVec *items) {
  int c_ship=0,c_stat=0,c_wayp=0;
  for(int i=0;i<items->n;i++){
    if(items->a[i].Type==tSHIP) c_ship++;
    else if(items->a[i].Type==tSTAT) c_stat++;
    else if(items->a[i].Type==tWAYP) c_wayp++;
  }
  ExData ex;
  ex.SelectedSize = c_ship+c_stat+c_wayp;
  ex.Size = c_ship+c_stat;
  ex.Type = (int*)xmalloc((size_t)ex.SelectedSize*sizeof(int));
  ex.ItemIndex = (int*)xmalloc((size_t)ex.SelectedSize*sizeof(int));
  ex.Amount = (double*)xmalloc((size_t)ex.SelectedSize*sizeof(double));
  ex.LatLonRad = (double*)xmalloc((size_t)ex.SelectedSize*4*sizeof(double));
  ex.LatLonDegMin = (double*)xmalloc((size_t)ex.SelectedSize*4*sizeof(double));
  int k=0;
  append_type_ex(items,tSHIP,&ex,&k);
  append_type_ex(items,tSTAT,&ex,&k);
  append_type_ex(items,tWAYP,&ex,&k);
  if(k!=ex.SelectedSize) die("build_exdata_no_ports mismatch");
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

static void orient_node_tour(int *tour, int len) {
  if (!tour || len <= 1) return;
  int pos0 = -1;
  for (int i = 0; i < len; i++) {
    if (tour[i] == 0) { pos0 = i; break; }
  }
  if (pos0 > 0) {
    int *rot = (int*)xmalloc((size_t)len * sizeof(int));
    int idx = 0;
    for (int i = pos0; i < len; i++) rot[idx++] = tour[i];
    for (int i = 0; i < pos0; i++) rot[idx++] = tour[i];
    memcpy(tour, rot, (size_t)len * sizeof(int));
    free(rot);
  }
  /* If 1 is immediately after 0, the cycle is reversed; flip it. */
  if (len > 1 && tour[1] == 1) {
    for (int i = 1, j = len - 1; i < j; i++, j--) {
      int tmp = tour[i];
      tour[i] = tour[j];
      tour[j] = tmp;
    }
  }
}


int __stdcall subtourelim(GRBmodel *model, void *cbdata, int where, void *usrdata) {
  (void)model;
  struct callback_data *d=(struct callback_data*)usrdata;
  int n=d->n;
  int numvars = d->numvars;
  int error=0;

  if(where==GRB_CB_MIPSOL){
    double *sol=(double*)xmalloc((size_t)numvars*sizeof(double));
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

static int solve_tsp_distance(GRBenv *env, const double *dist, int Size,
                              double timelimit, int verbose, double *out_obj,
                              int **out_tour, int *out_len) {
  int n = 2 * Size;
  GRBmodel *model = NULL;
  int error = 0;

  error = GRBnewmodel(env, &model, "segment", 0, NULL, NULL, NULL, NULL, NULL);
  if (error) goto QUIT;

  if (timelimit > 0.0) {
    GRBsetdblparam(env, "TimeLimit", timelimit);
  }
  GRBsetintparam(env, "Threads", 4);
  GRBsetintparam(env, "OutputFlag", verbose ? 1 : 0);
  GRBsetintparam(env, "LogToConsole", verbose ? 1 : 0);

  for (int i = 0; i < n; i++) {
    for (int j = 0; j < n; j++) {
      char vname[64];
      sprintf(vname, "e_%d_%d", i, j);
      error = GRBaddvar(model, 0, NULL, NULL, dist[i*n + j], 0.0, 1.0, GRB_BINARY, vname);
      if (error) goto QUIT;
    }
  }

  int *ind = (int*)xmalloc((size_t)n * sizeof(int));
  double *val = (double*)xmalloc((size_t)n * sizeof(double));
  for (int j = 0; j < n; j++) val[j] = 1.0;

  for (int i = 0; i < n; i++) {
    for (int j = 0; j < n; j++) ind[j] = i*n + j;
    error = GRBaddconstr(model, n, ind, val, GRB_EQUAL, 1.0, NULL);
    if (error) goto QUIT;
  }
  for (int j = 0; j < n; j++) {
    for (int i = 0; i < n; i++) ind[i] = i*n + j;
    error = GRBaddconstr(model, n, ind, val, GRB_EQUAL, 1.0, NULL);
    if (error) goto QUIT;
  }

  for (int i = 0; i < Size; i++) {
    int a = 2*i, b = 2*i+1;
    ind[0] = a*n + b; val[0] = 1.0;
    ind[1] = b*n + a; val[1] = 1.0;
    error = GRBaddconstr(model, 2, ind, val, GRB_EQUAL, 1.0, NULL);
    if (error) goto QUIT;
  }
  free(ind);
  free(val);

  for (int i = 0; i < n; i++) {
    error = GRBsetdblattrelement(model, GRB_DBL_ATTR_UB, i*n + i, 0.0);
    if (error) goto QUIT;
  }

  struct callback_data cb;
  cb.n = n;
  cb.numvars = n * n;
  error = GRBsetcallbackfunc(model, subtourelim, (void*)&cb);
  if (error) goto QUIT;
  error = GRBsetintparam(GRBgetenv(model), GRB_INT_PAR_LAZYCONSTRAINTS, 1);
  if (error) goto QUIT;

  error = GRBoptimize(model);
  if (error) goto QUIT;

  int status = 0;
  GRBgetintattr(model, GRB_INT_ATTR_STATUS, &status);
  if (status == GRB_OPTIMAL || status == GRB_TIME_LIMIT || status == GRB_SUBOPTIMAL) {
    error = GRBgetdblattr(model, GRB_DBL_ATTR_OBJVAL, out_obj);
    if (error) goto QUIT;
    if (out_tour && out_len) {
      int solcount = 0;
      error = GRBgetintattr(model, GRB_INT_ATTR_SOLCOUNT, &solcount);
      if (error) goto QUIT;
      if (solcount > 0) {
        double *sol = (double*)xmalloc((size_t)n*n*sizeof(double));
        error = GRBgetdblattrarray(model, GRB_DBL_ATTR_X, 0, n*n, sol);
        if (error) { free(sol); goto QUIT; }
        int *tour = (int*)xmalloc((size_t)n*sizeof(int));
        int len = 0;
        findsubtour_directed(n, sol, &len, tour);
        free(sol);
        *out_tour = tour;
        *out_len = len;
      } else {
        error = 1;
      }
    }
  } else {
    error = 1;
  }

QUIT:
  if (model) GRBfreemodel(model);
  return error;
}

static int solve_segment_distance(GRBenv *env, const ExData *orig,
                                  const int *station_ex, int n_station,
                                  const double start_rad[2], const double end_rad[2],
                                  double timelimit, double *out_dist,
                                  int **out_order, int *out_order_len) {
  int n_wayp = orig->SelectedSize - orig->Size;
  if (n_station == 0) {
    ExData seg;
    seg.Size = 1;
    seg.SelectedSize = 1 + n_wayp;
    seg.Type = (int*)xmalloc((size_t)seg.SelectedSize * sizeof(int));
    seg.ItemIndex = (int*)xmalloc((size_t)seg.SelectedSize * sizeof(int));
    seg.Amount = (double*)xmalloc((size_t)seg.SelectedSize * sizeof(double));
    seg.LatLonRad = (double*)xmalloc((size_t)seg.SelectedSize * 4 * sizeof(double));
    seg.LatLonDegMin = (double*)xmalloc((size_t)seg.SelectedSize * 4 * sizeof(double));

    seg.Type[0] = tSHIP;
    seg.ItemIndex[0] = -1;
    seg.Amount[0] = 0.0;
    seg.LatLonRad[0] = start_rad[0];
    seg.LatLonRad[1] = start_rad[1];
    seg.LatLonRad[2] = end_rad[0];
    seg.LatLonRad[3] = end_rad[1];
    for (int k = 0; k < 4; k++) seg.LatLonDegMin[k] = 0.0;

    for (int w = 0; w < n_wayp; w++) {
      int src = orig->Size + w;
      int dst = seg.Size + w;
      seg.Type[dst] = tWAYP;
      seg.ItemIndex[dst] = orig->ItemIndex[src];
      seg.Amount[dst] = 0.0;
      for (int k = 0; k < 4; k++) {
        seg.LatLonRad[dst*4 + k] = orig->LatLonRad[src*4 + k];
        seg.LatLonDegMin[dst*4 + k] = orig->LatLonDegMin[src*4 + k];
      }
    }

    double *dist = NULL;
    int *fsb = NULL;
    build_waypoint_dist(&seg, NULL, 0, &dist, &fsb, NULL, NULL, NULL);
    int n = 2 * seg.Size;
    *out_dist = dist[0*n + 1];
    if (out_order && out_order_len) {
      *out_order = NULL;
      *out_order_len = 0;
    }
    free(dist);
    free(fsb);
    free_exdata(&seg);
    return 0;
  }

  ExData seg;
  seg.Size = 1 + n_station;
  seg.SelectedSize = seg.Size + n_wayp;
  seg.Type = (int*)xmalloc((size_t)seg.SelectedSize * sizeof(int));
  seg.ItemIndex = (int*)xmalloc((size_t)seg.SelectedSize * sizeof(int));
  seg.Amount = (double*)xmalloc((size_t)seg.SelectedSize * sizeof(double));
  seg.LatLonRad = (double*)xmalloc((size_t)seg.SelectedSize * 4 * sizeof(double));
  seg.LatLonDegMin = (double*)xmalloc((size_t)seg.SelectedSize * 4 * sizeof(double));

  seg.Type[0] = tSHIP;
  seg.ItemIndex[0] = -1;
  seg.Amount[0] = 0.0;
  seg.LatLonRad[0] = start_rad[0];
  seg.LatLonRad[1] = start_rad[1];
  seg.LatLonRad[2] = end_rad[0];
  seg.LatLonRad[3] = end_rad[1];
  for (int k = 0; k < 4; k++) seg.LatLonDegMin[k] = 0.0;

  for (int i = 0; i < n_station; i++) {
    int ex_idx = station_ex[i];
    int dst = 1 + i;
    seg.Type[dst] = tSTAT;
    seg.ItemIndex[dst] = ex_idx;
    seg.Amount[dst] = orig->Amount[ex_idx];
    for (int k = 0; k < 4; k++) {
      seg.LatLonRad[dst*4 + k] = orig->LatLonRad[ex_idx*4 + k];
      seg.LatLonDegMin[dst*4 + k] = orig->LatLonDegMin[ex_idx*4 + k];
    }
  }

  for (int w = 0; w < n_wayp; w++) {
    int src = orig->Size + w;
    int dst = seg.Size + w;
    seg.Type[dst] = tWAYP;
    seg.ItemIndex[dst] = orig->ItemIndex[src];
    seg.Amount[dst] = 0.0;
    for (int k = 0; k < 4; k++) {
      seg.LatLonRad[dst*4 + k] = orig->LatLonRad[src*4 + k];
      seg.LatLonDegMin[dst*4 + k] = orig->LatLonDegMin[src*4 + k];
    }
  }

  double *dist = NULL;
  int *fsb = NULL;
  build_waypoint_dist(&seg, NULL, 0, &dist, &fsb, NULL, NULL, NULL);

  int n = 2 * seg.Size;
  /* Force closure direction: end->start is free, start->end is expensive. */
  dist[1*n + 0] = 0.0;
  fsb[1*n + 0] = 1;
  dist[0*n + 1] = 1e9;
  fsb[0*n + 1] = 1;

  int *node_tour = NULL;
  int node_len = 0;
  int error = solve_tsp_distance(env, dist, seg.Size, timelimit, 0, out_dist,
                                 out_order ? &node_tour : NULL,
                                 out_order ? &node_len : NULL);

  if (!error && out_order && out_order_len && node_tour) {
    orient_node_tour(node_tour, node_len);
    int letour_len = 0;
    int *letour = node_tour_to_letour(node_tour, node_len, seg.Size, &letour_len);
    int *mapped = (int*)xmalloc((size_t)letour_len * sizeof(int));
    int m = 0;
    for (int i = 0; i < letour_len; i++) {
      if (letour[i] == 0) continue;
      int sign = letour[i] < 0 ? -1 : 1;
      int local_idx = abs(letour[i]) - 1;
      if (local_idx < 0 || local_idx >= n_station) continue;
      mapped[m++] = sign * station_ex[local_idx];
    }
    *out_order = mapped;
    *out_order_len = m;
    free(letour);
  }

  if (node_tour) free(node_tour);

  free(dist);
  free(fsb);
  free_exdata(&seg);
  return error;
}

static void shuffle_visits(Visit *visits, int n) {
  for (int i = n - 1; i > 0; i--) {
    int j = rand() % (i + 1);
    Visit tmp = visits[i];
    visits[i] = visits[j];
    visits[j] = tmp;
  }
}

static int closest_port_ex(const ExData *ex, const double *full_dist, int full_m,
                           int from_ex, double *out_dist) {
  double best = 1e300;
  int best_ex = -1;
  for (int i = 0; i < ex->Size; i++) {
    if (ex->Type[i] != tPORT) continue;
    int a0 = 2*from_ex;
    int a1 = 2*from_ex + 1;
    int b0 = 2*i;
    int b1 = 2*i + 1;
    double d0 = full_dist[a0*full_m + b0];
    double d1 = full_dist[a0*full_m + b1];
    double d2 = full_dist[a1*full_m + b0];
    double d3 = full_dist[a1*full_m + b1];
    double d = d0;
    if (d1 < d) d = d1;
    if (d2 < d) d = d2;
    if (d3 < d) d = d3;
    if (d < best) { best = d; best_ex = i; }
  }
  if (out_dist) *out_dist = best;
  return best_ex;
}

static Visit *build_capacity_visits(const ExData *ex, const ItemVec *items,
                                    const double *full_dist, int full_m,
                                    const int *station_order, int n_station,
                                    int max_segments, double ship_cap,
                                    int *out_n) {
  int cap = n_station * 2 + 8;
  int n = 0;
  Visit *visits = (Visit*)xmalloc((size_t)cap * sizeof(Visit));
  double load = 0.0;
  int last_stat = -1;

  if (ship_cap <= 0.0) ship_cap = 1.0;
  if (max_segments < 1) max_segments = 1;

  for (int i = 0; i < n_station; i++) {
    int st = station_order[i];
    double amt = ex->Amount[st];

    if (load > 0.0 && load + amt > ship_cap) {
      double best_dist = 0.0;
      int port_ex = closest_port_ex(ex, full_dist, full_m, last_stat, &best_dist);
      if (port_ex >= 0) {
        if (n == cap) { cap *= 2; visits = (Visit*)realloc(visits, (size_t)cap * sizeof(Visit)); if(!visits) die("OOM"); }
        visits[n].type = tPORT;
        visits[n].ex_idx = port_ex;
        n++;
        Item *pt = &items->a[ex->ItemIndex[port_ex]];
        printf("Insert port before STAT-%d: PORT-%d", st, port_ex);
        if (pt->Name) printf("(%s)", pt->Name);
        printf(" load=%.0f dist=%.3f\n", load, best_dist);
        load = 0.0;
        last_stat = -1;
      } else {
        printf("Insert port: none available before STAT-%d (load=%.0f)\n", st, load);
      }
    }

    if (n == cap) { cap *= 2; visits = (Visit*)realloc(visits, (size_t)cap * sizeof(Visit)); if(!visits) die("OOM"); }
    visits[n].type = tSTAT;
    visits[n].ex_idx = st;
    n++;
    load += amt;
    last_stat = st;

    if (amt > ship_cap) {
      printf("Warning: STAT-%d amount=%.0f exceeds ship capacity %.0f\n", st, amt, ship_cap);
    }

    if (load >= ship_cap && i + 1 < n_station) {
      double best_dist = 0.0;
      int port_ex = closest_port_ex(ex, full_dist, full_m, st, &best_dist);
      if (port_ex < 0) {
        printf("Insert port: none available after STAT-%d (load=%.0f)\n", st, load);
        continue;
      }
      if (n == cap) { cap *= 2; visits = (Visit*)realloc(visits, (size_t)cap * sizeof(Visit)); if(!visits) die("OOM"); }
      visits[n].type = tPORT;
      visits[n].ex_idx = port_ex;
      n++;
      Item *pt = &items->a[ex->ItemIndex[port_ex]];
      printf("Insert port after STAT-%d: PORT-%d", st, port_ex);
      if (pt->Name) printf("(%s)", pt->Name);
      printf(" load=%.0f dist=%.3f\n", load, best_dist);
      load = 0.0;
      last_stat = -1;
    }
  }

  *out_n = n;
  return visits;
}

static void print_visit_list(const ExData *ex, const ItemVec *items,
                             const Visit *visits, int n) {
  printf("Visit order (%d):", n);
  for (int i = 0; i < n; i++) {
    const char *label = (visits[i].type == tPORT) ? "PORT" : "STAT";
    printf(" %s-%d", label, visits[i].ex_idx);
    if (visits[i].type == tPORT) {
      Item *pt = &items->a[ex->ItemIndex[visits[i].ex_idx]];
      if (pt->Name) printf("(%s)", pt->Name);
    }
  }
  printf("\n");
}

static void print_segment_plan(const ExData *ex, const ItemVec *items,
                               const Visit *visits, int n_visits) {
  int seg = 1;
  int first = 1;
  printf("Segment plan:\n");
  printf("  Segment %d -> START\n", seg);
  for (int i = 0; i < n_visits; i++) {
    if (visits[i].type == tSTAT) {
      if (first) {
        printf("    stations:");
        first = 0;
      }
      printf(" %d", visits[i].ex_idx);
      continue;
    }
    if (visits[i].type == tPORT) {
      Item *pt = &items->a[ex->ItemIndex[visits[i].ex_idx]];
      if (!first) printf("\n");
      printf("    end: PORT-%d", visits[i].ex_idx);
      if (pt->Name) printf("(%s)", pt->Name);
      printf("\n");
      seg++;
      printf("  Segment %d -> PORT-%d\n", seg, visits[i].ex_idx);
      first = 1;
    }
  }
  if (!first) printf("\n");
  printf("    end: BOAT-END\n");
}

static SegmentResult *evaluate_visit_segments(const ExData *ex,
                                              const Visit *visits, int n_visits,
                                              int *out_nseg, double timelimit,
                                              int **out_tour, int *out_tour_len) {
  int max_seg = 1;
  for (int i = 0; i < n_visits; i++) {
    if (visits[i].type == tPORT) max_seg++;
  }
  SegmentResult *results = (SegmentResult*)xcalloc((size_t)max_seg, sizeof(SegmentResult));

  int *seg_stats = (int*)xmalloc((size_t)n_visits * sizeof(int));
  int seg_stat_count = 0;
  double seg_amount = 0.0;

  int seg_count = 0;
  int current_start_ex = 0;
  int current_start_type = tSHIP;
  double start_rad[2] = { ex->LatLonRad[0], ex->LatLonRad[1] };

  int tour_cap = n_visits + 2;
  int tour_len = 0;
  int *tour = (int*)xmalloc((size_t)tour_cap * sizeof(int));
  tour[tour_len++] = 0;

  GRBenv *env = NULL;
  if (GRBloadenv(&env, NULL) != 0) {
    free(seg_stats);
    free(tour);
    free(results);
    return NULL;
  }
  GRBsetintparam(env, "OutputFlag", 0);
  GRBsetintparam(env, "LogToConsole", 0);

  for (int i = 0; i < n_visits; i++) {
    if (visits[i].type == tSTAT) {
      seg_stats[seg_stat_count++] = visits[i].ex_idx;
      seg_amount += ex->Amount[visits[i].ex_idx];
      continue;
    }
    if (visits[i].type != tPORT) continue;

    int end_ex = visits[i].ex_idx;
    double end_rad[2] = { ex->LatLonRad[end_ex*4 + 0], ex->LatLonRad[end_ex*4 + 1] };
    printf("Segment %d eval: start=%s-%d end=PORT-%d stations=%d (%s)\n",
           seg_count + 1,
           current_start_type == tSHIP ? "BOAT" : "PORT",
           current_start_ex,
           end_ex,
           seg_stat_count,
           seg_stat_count == 0 ? "direct" : "optimize");
    double dist = 0.0;
    int *seg_order = NULL;
    int seg_order_len = 0;
    if (solve_segment_distance(env, ex, seg_stats, seg_stat_count, start_rad, end_rad,
                               timelimit, &dist, &seg_order, &seg_order_len) != 0) {
      dist = -1.0;
    }

    if (seg_order_len > 0) {
      printf("  Segment %d order:", seg_count + 1);
      for (int k = 0; k < seg_order_len; k++) printf(" %d", seg_order[k]);
      printf("\n");
      if (tour_len + seg_order_len + 2 > tour_cap) {
        tour_cap = (tour_len + seg_order_len + 2) * 2;
        tour = (int*)realloc(tour, (size_t)tour_cap * sizeof(int));
        if (!tour) die("OOM");
      }
      for (int k = 0; k < seg_order_len; k++) tour[tour_len++] = seg_order[k];
    }
    if (seg_order) free(seg_order);
    if (tour_len + 2 > tour_cap) {
      tour_cap *= 2;
      tour = (int*)realloc(tour, (size_t)tour_cap * sizeof(int));
      if (!tour) die("OOM");
    }
    tour[tour_len++] = end_ex;

    results[seg_count].distance = dist;
    results[seg_count].total_amount = seg_amount;
    results[seg_count].n_stations = seg_stat_count;
    results[seg_count].start_ex = current_start_ex;
    results[seg_count].end_ex = end_ex;
    results[seg_count].start_type = current_start_type;
    results[seg_count].end_type = tPORT;
    seg_count++;

    current_start_ex = end_ex;
    current_start_type = tPORT;
    start_rad[0] = end_rad[0];
    start_rad[1] = end_rad[1];
    seg_stat_count = 0;
    seg_amount = 0.0;
  }

  /* final segment to ship end */
  {
    double end_rad[2] = { ex->LatLonRad[2], ex->LatLonRad[3] };
    printf("Segment %d eval: start=%s-%d end=BOAT-END stations=%d (%s)\n",
           seg_count + 1,
           current_start_type == tSHIP ? "BOAT" : "PORT",
           current_start_ex,
           seg_stat_count,
           seg_stat_count == 0 ? "direct" : "optimize");
    double dist = 0.0;
    int *seg_order = NULL;
    int seg_order_len = 0;
    if (solve_segment_distance(env, ex, seg_stats, seg_stat_count, start_rad, end_rad,
                               timelimit, &dist, &seg_order, &seg_order_len) != 0) {
      dist = -1.0;
    }
    if (seg_order_len > 0) {
      printf("  Segment %d order:", seg_count + 1);
      for (int k = 0; k < seg_order_len; k++) printf(" %d", seg_order[k]);
      printf("\n");
      if (tour_len + seg_order_len + 1 > tour_cap) {
        tour_cap = (tour_len + seg_order_len + 1) * 2;
        tour = (int*)realloc(tour, (size_t)tour_cap * sizeof(int));
        if (!tour) die("OOM");
      }
      for (int k = 0; k < seg_order_len; k++) tour[tour_len++] = seg_order[k];
    }
    if (seg_order) free(seg_order);
    results[seg_count].distance = dist;
    results[seg_count].total_amount = seg_amount;
    results[seg_count].n_stations = seg_stat_count;
    results[seg_count].start_ex = current_start_ex;
    results[seg_count].end_ex = 0;
    results[seg_count].start_type = current_start_type;
    results[seg_count].end_type = tSHIP;
    seg_count++;
  }

  GRBfreeenv(env);
  free(seg_stats);

  *out_nseg = seg_count;
  if (out_tour && out_tour_len) {
    *out_tour = tour;
    *out_tour_len = tour_len;
  } else {
    free(tour);
  }
  return results;
}

/* ---------- Main ---------- */
int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "Usage: %s <datafile.dat> <ship_id 1..4> [--time-limit <sec>] [--cap-time-limit <sec>] [--stall-passes <n>] [--write-dat <out.dat>] [--verbose-init]\n", argv[0]);
    return 1;
  }

  const char *file = argv[1];
  int ship_id = atoi(argv[2]);
  double timelimit = 0.0;
  double cap_timelimit = 120.0;
  int stall_passes = 3;
  int verbose_init = 0;
  const char *write_dat = NULL;
  for (int i = 3; i < argc; i++) {
    if (strcmp(argv[i], "--no-ports") == 0) {
      die("--no-ports is not supported in matheur (ports define segments)");
    } else if (strcmp(argv[i], "--write-dat") == 0) {
      if (i + 1 < argc) {
        write_dat = argv[i + 1];
        i++;
      } else {
        die("--write-dat requires a path");
      }
    } else if (strcmp(argv[i], "--time-limit") == 0) {
      if (i + 1 < argc) {
        timelimit = atof(argv[i + 1]);
        i++;
      } else {
        die("--time-limit requires a value");
      }
    } else if (strcmp(argv[i], "--cap-time-limit") == 0) {
      if (i + 1 < argc) {
        cap_timelimit = atof(argv[i + 1]);
        i++;
      } else {
        die("--cap-time-limit requires a value");
      }
    } else if (strcmp(argv[i], "--stall-passes") == 0) {
      if (i + 1 < argc) {
        stall_passes = atoi(argv[i + 1]);
        i++;
      } else {
        die("--stall-passes requires a value");
      }
    } else if (strncmp(argv[i], "--time-limit=", 13) == 0) {
      timelimit = atof(argv[i] + 13);
    } else if (strncmp(argv[i], "--cap-time-limit=", 17) == 0) {
      cap_timelimit = atof(argv[i] + 17);
    } else if (strncmp(argv[i], "--stall-passes=", 15) == 0) {
      stall_passes = atoi(argv[i] + 15);
    } else if (strcmp(argv[i], "--verbose-init") == 0) {
      verbose_init = 1;
    } else {
      timelimit = atof(argv[i]);
    }
  }

  const char *ship_names[] = { "Árni Friðriksson", "Bjarni Sæmundsson", "Gullver", "Breki" };
  if (ship_id < 1 || ship_id > 4) die("ship_id must be 1..4");
  const char *ship = ship_names[ship_id-1];

  printf("Ship: %s\n", ship);
  srand((unsigned)time(NULL));

  /* read dat */
  ItemVec items; vec_init(&items);
  double ShipCap = 0.0;
  readDat_C(file, ship, &items, &ShipCap, 0);
  printf("ShipCap: %.0f\n", ShipCap);

  ExData ex = build_exdata(&items);
  /* Build waypoint-aware feasibility + distance for node graph of size n = 2*Size */
  int Size = ex.Size;
  int n = 2 * Size;

  double *dist = NULL;
  int *fsb = NULL;
  double *full_dist = NULL;
  int *full_fsb = NULL;
  int full_m = 0;
  build_waypoint_dist(&ex, NULL, 0, &dist, &fsb, &full_dist, &full_fsb, &full_m);

  /* special closure between nodes 0 and 1 */
  dist[0*n + 1] = 0.0;
  dist[1*n + 0] = 0.0;
  fsb[0*n + 1] = 1;
  fsb[1*n + 0] = 1;

  ExData ex_np = build_exdata_no_ports(&items);
  int np_n = 2 * ex_np.Size;
  double *dist_np = NULL;
  int *fsb_np = NULL;
  build_waypoint_dist(&ex_np, NULL, 0, &dist_np, &fsb_np, NULL, NULL, NULL);
  dist_np[0*np_n + 1] = 0.0;
  dist_np[1*np_n + 0] = 0.0;
  fsb_np[0*np_n + 1] = 1;
  fsb_np[1*np_n + 0] = 1;

  GRBenv *init_env = NULL;
  if (GRBloadenv(&init_env, NULL) != 0) die("init env failed");
  GRBsetintparam(init_env, "OutputFlag", 0);
  GRBsetintparam(init_env, "LogToConsole", 0);
  double init_obj = 0.0;
  int *init_tour = NULL;
  int init_len = 0;
  if (solve_tsp_distance(init_env, dist_np, ex_np.Size, timelimit, verbose_init, &init_obj,
                         &init_tour, &init_len) != 0) {
    die("initial no-port solve failed");
  }
  printf("Initial no-port objective: %.3f\n", init_obj);
  GRBfreeenv(init_env);

  int letour_len = 0;
  int *letour = node_tour_to_letour(init_tour, init_len, ex_np.Size, &letour_len);
  int *station_order = (int*)xmalloc((size_t)letour_len * sizeof(int));
  int n_station = 0;
  for (int i = 0; i < letour_len; i++) {
    if (letour[i] == 0) continue;
    station_order[n_station++] = abs(letour[i]);
  }
  printf("Initial station order (no ports):");
  for (int i = 0; i < n_station; i++) printf(" %d", station_order[i]);
  printf("\n");
  free(letour);
  free(init_tour);
  free(dist_np);
  free(fsb_np);
  free_exdata(&ex_np);

  double total_amount = 0.0;
  for (int i = 0; i < ex.SelectedSize; i++) {
    if (ex.Type[i] == tSTAT) total_amount += ex.Amount[i];
  }
  int init_segments = 1;
  if (ShipCap > 0.0) {
    init_segments = (int)ceil(total_amount / ShipCap);
    if (init_segments < 1) init_segments = 1;
  }
  printf("Init segments min: %d (total=%.1f, ship_cap=%.1f)\n",
         init_segments, total_amount, ShipCap);

  int n_visits = 0;
  Visit *visits = build_capacity_visits(&ex, &items, full_dist, full_m,
                                        station_order, n_station,
                                        init_segments, ShipCap, &n_visits);
  free(station_order);
  int init_built = count_segments(visits, n_visits);
  printf("Init segments built: %d (min=%d)\n", init_built, init_segments);
  if (init_built > init_segments) {
    printf("Init note: extra segments added to respect ship capacity.\n");
  }

  print_visit_list(&ex, &items, visits, n_visits);
  printf("Initial segment plan:\n");
  print_segment_plan(&ex, &items, visits, n_visits);

  int nseg_max = count_segments(visits, n_visits);
  int port_count = nseg_max - 1;
  int *dirty = NULL;
  if (port_count > 0) {
    dirty = (int*)xcalloc((size_t)port_count, sizeof(int));
    for (int i = 0; i < port_count; i++) dirty[i] = 1;
  }

  char *plot_path = NULL;
  char *csv_path = NULL;
  char *cap_csv_path = NULL;
  FILE *csv = NULL;
  FILE *cap_csv = NULL;
  if (write_dat) {
    const char *dot = strrchr(write_dat, '.');
    size_t base = dot ? (size_t)(dot - write_dat) : strlen(write_dat);
    plot_path = (char*)xmalloc(base + 5);
    memcpy(plot_path, write_dat, base);
    memcpy(plot_path + base, ".txt", 5);
    csv_path = (char*)xmalloc(base + 5);
    memcpy(csv_path, write_dat, base);
    memcpy(csv_path + base, ".csv", 5);
    cap_csv_path = (char*)xmalloc(base + 9);
    memcpy(cap_csv_path, write_dat, base);
    memcpy(cap_csv_path + base, ".cap.csv", 9);
  }
  if (csv_path) {
    csv = fopen(csv_path, "w");
    if (!csv) {
      perror("fopen(csv)");
    } else {
      fputs("attempts,changed,total,best,stall", csv);
      if (nseg_max > 0) fputc(',', csv);
      for (int s = 0; s < nseg_max; s++) {
        fprintf(csv, "seg%d_distance,seg%d_stations,seg%d_amount", s + 1, s + 1, s + 1);
        if (s + 1 < nseg_max) fputc(',', csv);
      }
      fputc('\n', csv);
    }
  }
  if (cap_csv_path) {
    cap_csv = fopen(cap_csv_path, "w");
    if (!cap_csv) {
      perror("fopen(cap_csv)");
    } else {
      fputs("pass,boundary,port_ex,left_stations,right_stations,load_left,load_right,n_nodes,status,solcount,obj,bound,gap,runtime,nodes,changed\n",
            cap_csv);
    }
  }

  int nseg = 0;
  int *seg_tour = NULL;
  int seg_tour_len = 0;
  SegmentResult *segs = NULL;
  double best_total = 1e300;
  int best_pass = 0;
  int pass = 0;
  int changed = 0;
  int any_change = 0;
  int stall_count = 0;

  /* Baseline: evaluate initial segmented solution before any boundary moves. */
  {
    int nseg_init = 0;
    int *seg_tour_init = NULL;
    int seg_tour_len_init = 0;
    SegmentResult *segs_init = evaluate_visit_segments(&ex, visits, n_visits, &nseg_init,
                                                       timelimit, &seg_tour_init, &seg_tour_len_init);
    if (!segs_init) die("segment evaluation failed");
    double total_init = (seg_tour_init && seg_tour_len_init > 0)
                          ? letour_distance_total(seg_tour_init, seg_tour_len_init, dist, Size)
                          : sum_segment_distance(segs_init, nseg_init);
    best_total = total_init;
    best_pass = 0;
    if (seg_tour_init && seg_tour_len_init > 0 && write_dat) {
      write_route_dat(write_dat, &items, &ex, seg_tour_init, seg_tour_len_init);
    }
    if (seg_tour_init && seg_tour_len_init > 0 && plot_path) {
      write_plot_bundle_letour(plot_path,
                               Size, ex.SelectedSize,
                               seg_tour_init, seg_tour_len_init,
                               ex.Type, ex.Amount, ex.LatLonRad,
                               &items, &ex);
    }
    printf("PROGRESS pass=0 changed=0 total=%.3f best=%.3f nseg=%d stall=0/%d\n",
           total_init, best_total, nseg_init, stall_passes);
    if (csv) {
      fprintf(csv, "0,0,%.3f,%.3f,0", total_init, best_total);
      if (nseg_max > 0) fputc(',', csv);
      for (int s = 0; s < nseg_max; s++) {
        if (s < nseg_init && segs_init[s].distance >= 0.0) {
          fprintf(csv, "%.3f,%d,%.0f",
                  segs_init[s].distance,
                  segs_init[s].n_stations,
                  segs_init[s].total_amount);
        } else if (s < nseg_init) {
          fprintf(csv, "nan,%d,%.0f",
                  segs_init[s].n_stations,
                  segs_init[s].total_amount);
        } else {
          fprintf(csv, "nan,0,0");
        }
        if (s + 1 < nseg_max) fputc(',', csv);
      }
      fputc('\n', csv);
      fflush(csv);
    }
    free(segs_init);
    free(seg_tour_init);
  }

  GRBenv *guard_env = NULL;
  if (GRBloadenv(&guard_env, NULL) != 0) die("guard env failed");
  GRBsetintparam(guard_env, "OutputFlag", 0);
  GRBsetintparam(guard_env, "LogToConsole", 0);

  for (;;) {
    pass++;
    changed = optimize_boundaries_one_pass(&visits, &n_visits, &ex, ShipCap,
                                           cap_timelimit, pass, dirty,
                                           port_count, cap_csv,
                                           guard_env, timelimit);
    if (changed) any_change = 1;

    int nseg_new = 0;
    int *seg_tour_new = NULL;
    int seg_tour_len_new = 0;
    SegmentResult *segs_new = evaluate_visit_segments(&ex, visits, n_visits, &nseg_new,
                                                      timelimit, &seg_tour_new, &seg_tour_len_new);
    if (!segs_new) die("segment evaluation failed");

    double total = (seg_tour_new && seg_tour_len_new > 0)
                     ? letour_distance_total(seg_tour_new, seg_tour_len_new, dist, Size)
                     : sum_segment_distance(segs_new, nseg_new);
    if (total >= 0.0 && total + 1e-9 < best_total) {
      best_total = total;
      best_pass = pass;
      stall_count = 0;
      if (seg_tour_new && seg_tour_len_new > 0 && write_dat) {
        write_route_dat(write_dat, &items, &ex, seg_tour_new, seg_tour_len_new);
      }
      if (seg_tour_new && seg_tour_len_new > 0 && plot_path) {
        write_plot_bundle_letour(plot_path,
                                 Size, ex.SelectedSize,
                                 seg_tour_new, seg_tour_len_new,
                                 ex.Type, ex.Amount, ex.LatLonRad,
                                 &items, &ex);
      }
    } else {
      stall_count++;
    }

    printf("PROGRESS pass=%d changed=%d total=%.3f best=%.3f nseg=%d stall=%d/%d\n",
           pass, changed, total, best_total, nseg_new, stall_count, stall_passes);

    if (csv) {
      fprintf(csv, "%d,%d,", pass, changed);
      if (total >= 0.0) fprintf(csv, "%.3f,%.3f", total, best_total);
      else fprintf(csv, "nan,%.3f", best_total);
      fprintf(csv, ",%d", stall_count);
      if (nseg_max > 0) fputc(',', csv);
      for (int s = 0; s < nseg_max; s++) {
        if (s < nseg_new && segs_new[s].distance >= 0.0) {
          fprintf(csv, "%.3f,%d,%.0f",
                  segs_new[s].distance,
                  segs_new[s].n_stations,
                  segs_new[s].total_amount);
        } else if (s < nseg_new) {
          fprintf(csv, "nan,%d,%.0f",
                  segs_new[s].n_stations,
                  segs_new[s].total_amount);
        } else {
          fprintf(csv, "nan,0,0");
        }
        if (s + 1 < nseg_max) fputc(',', csv);
      }
      fputc('\n', csv);
      fflush(csv);
    }

    free(segs);
    free(seg_tour);
    segs = segs_new;
    seg_tour = seg_tour_new;
    seg_tour_len = seg_tour_len_new;
    nseg = nseg_new;
    if (!changed) break;
    if (stall_passes > 0 && stall_count >= stall_passes) {
      printf("Stopping: no improvement for %d passes.\n", stall_passes);
      break;
    }
  }

  if (guard_env) GRBfreeenv(guard_env);
  free(dirty);

  if (any_change) {
    printf("Capacity boundary sweep completed with changes.\n");
  } else {
    printf("Capacity boundary sweep: no changes.\n");
  }
  if (best_total < 1e299) {
    printf("Best pass %d total distance: %.3f\n", best_pass, best_total);
  }
  printf("Updated segment plan:\n");
  print_segment_plan(&ex, &items, visits, n_visits);

  int use_segment_plot = (seg_tour && seg_tour_len > 0);
  if (use_segment_plot) {
    print_segment_stats_forward(seg_tour, seg_tour_len, &ex, &items, dist);
  } else {
    for (int i = 0; i < nseg; i++) {
      const char *start_name = "START";
      const char *end_name = "END";
      if (segs[i].start_type == tPORT) {
        Item *pt = &items.a[ex.ItemIndex[segs[i].start_ex]];
        start_name = pt->Name ? pt->Name : "PORT";
      } else if (segs[i].start_type == tSHIP) {
        Item *ship_item = &items.a[ex.ItemIndex[0]];
        start_name = ship_item->Name ? ship_item->Name : "BOAT";
      }
      if (segs[i].end_type == tPORT) {
        Item *pt = &items.a[ex.ItemIndex[segs[i].end_ex]];
        end_name = pt->Name ? pt->Name : "PORT";
      } else if (segs[i].end_type == tSHIP) {
        Item *ship_item = &items.a[ex.ItemIndex[0]];
        end_name = ship_item->Name ? ship_item->Name : "BOAT";
      }
      if (segs[i].distance < 0.0) {
        printf("Segment %d: %s -> %s | stations=%d distance=n/a amount=%.0f\n",
               i + 1, start_name, end_name, segs[i].n_stations, segs[i].total_amount);
      } else {
        printf("Segment %d: %s -> %s | stations=%d distance=%.3f amount=%.0f\n",
               i + 1, start_name, end_name, segs[i].n_stations, segs[i].distance, segs[i].total_amount);
      }
    }
  }
  double base_total = (use_segment_plot && seg_tour)
                        ? letour_distance_total(seg_tour, seg_tour_len, dist, Size)
                        : sum_segment_distance(segs, nseg);
  if (base_total >= 0.0) {
    printf("Segmented total distance: %.3f\n", base_total);
  }

  printf("SelectedSize=%d  Size=%d\n", ex.SelectedSize, ex.Size);

  free(dist);
  free(fsb);
  free(full_dist);
  free(full_fsb);
  free(segs);
  free(visits);
  free(seg_tour);
  free(plot_path);
  if (csv) fclose(csv);
  free(csv_path);
  if (cap_csv) fclose(cap_csv);
  free(cap_csv_path);
  free_exdata(&ex);
  vec_free(&items);
  return 0;
}
