// gcc -O3 -fPIC -shared -o libutils.dylib utils.c -lm
// gcc -O3 -fPIC -shared -o libutils.so utils.c -lm
// gcc -O3 -shared -o utils.dll utils.c -lm 

#include <math.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN  // Windows platform
  #include <windows.h>
  #define EXPORT __declspec(dllexport)
#else  // POSIX platforms (Linux/macOS)
  #define EXPORT
#endif

typedef struct {
  int    *Elite ;
  int    *Type;
  double *LatLonRad[4] ;
  double *StartEnd ;
  int    *Fixed ;
  int     Size, SelectedSize;
  double *DistMtrx, *Graph;
  double *Amount, *Before, *After, *ExtraTime;
  int *FsbleLink;
  double Capacity;
} PARAMS, *PPARAMS ;


typedef struct {
  double MINLAT, MINLON, MAXLAT, MAXLON;
  int n, N[3]; // no more than 3 islands in a map for now
  double *LatDeg[3], *LonDeg[3];
} MAPINFO;

#define SHIP 1
#define STAT 2
#define WAYP 3
#define ENDP 4
#define PORT 5

#define BOOL int
#define FALSE 0
#define TRUE 1

MAPINFO  MAP[2]; // two maps!
#define iMAP 0

#define PI       3.14159265358979323846264338327950288419716939937510582097494459230781640628

double degmin2deg(double degmin) {

  double min ;

  if (fabs(degmin)<10000)
    degmin = degmin*100 ;
  min = (degmin/100)-floor(degmin/10000.)*100. ;
  return(((degmin+(200.0/3.0)*min)/10000)) ;
}

void deg2point__ (double *x, double *y, double *Lat, double *Lon, int length, int Norm) {

  double lat65, x65, M, M65, Diff, lat, scale, lon ;
  int    i ;
  double MAXLAT = MAP[iMAP].MAXLAT;
  double MINLAT = MAP[iMAP].MINLAT;

  /* at 65lat 18lon */
  lat65 = 65.*PI/180. ;
  x65 = (111415.13*cos(lat65)-94.55*cos(3*lat65)+0.12*cos(5*lat65))/60 ;
  M65 = 7915.704456*log10(tan(PI/4+lat65/2))
           - sin(lat65)*(23.110771+0.052051*(sin(lat65))*sin(lat65)) ;

  /* Automatic Scaling */
  lat = MAXLAT*PI/180 ;
  M = 7915.704456*log10(tan(PI/4+lat/2))-sin(lat)*(23.110771+0.052051*(sin(lat))*sin(lat65)) ;
  Diff = M-M65 ;
  lat = MINLAT*PI/180 ;
  M = 7915.704456*log10(tan(PI/4+lat/2))-sin(lat)*(23.110771+0.052051*(sin(lat))*sin(lat65)) ;
  Diff = Diff + M65 - M ;
  scale = (double)Norm/(Diff*x65) ;
  x65 = scale*x65 ;

  for (i=0; i<length; i++) {
    lon = Lon[i];
    lat = Lat[i]*PI/180 ;
    M = 7915.704456*log10(tan(PI/4+lat/2))-sin(lat)*(23.110771+0.052051*(sin(lat))*sin(lat65)) ;
    Diff = M65-M ;
    y[i] = (int) (Diff*x65) ;
    x[i] = (int) ((lon+18)*x65*60) ;
  }
}



void deg2point (double *x, double *y, double *Lat, double *Lon, int length, int Norm) {

  double lat65, x65, M, M65, Diff, lat, scale, lon ;
  int    i ;

  /* at 65�lat 18�lon */
  lat65 = 65.*PI/180. ;
  x65 = (111415.13*cos(lat65)-94.55*cos(3*lat65)+0.12*cos(5*lat65))/60 ;
  M65 = 7915.704456*log10(tan(PI/4+lat65/2))
           - sin(lat65)*(23.110771+0.052051*(sin(lat65))*sin(lat65)) ;

  /* Automatic Scaling */
  lat = MAP[iMAP].MAXLAT*PI/180 ;
  M = 7915.704456*log10(tan(PI/4+lat/2))-sin(lat)*(23.110771+0.052051*(sin(lat))*sin(lat65)) ;
  Diff = M-M65 ;
  lat = MAP[iMAP].MINLAT*PI/180 ;
  M = 7915.704456*log10(tan(PI/4+lat/2))-sin(lat)*(23.110771+0.052051*(sin(lat))*sin(lat65)) ;
  Diff = Diff + M65 - M ;
  scale = (double)Norm/(Diff*x65) ;
  x65 = scale*x65 ;

  for (i=0; i<length; i++) {
    lon = Lon[i];
    lat = Lat[i]*PI/180 ;
    M = 7915.704456*log10(tan(PI/4+lat/2))-sin(lat)*(23.110771+0.052051*(sin(lat))*sin(lat65)) ;
    Diff = M65-M ;
    y[i] = (int) (Diff*x65) ;
    x[i] = (int) ((lon+18)*x65*60) ;
  }
}

/******************************************************************************
 ArcDist
 Comment:
 Computes the distance (miles) between two points given their longitude and
 latitude in radians
 ******************************************************************************/
double arcdist(double lat1, double lon1, double lat2, double lon2) {

  double r = 3437.905 ; /* Earth radius in miles */
  double angle = sin(lat1)*sin(lat2)+cos(lat1)*cos(lat2)*cos(lon1-lon2) ;
 
  return(r*acos(angle)) ; /* may want to skip multiplying by r to increase speed */
}

/* Uses the dot product to determine if two line intercept
   I think we should define waypoint along the coast to use for seeing if we cross the land */
int intersects(double s0x[2], double s0y[2], double s1x[2], double s1y[2]) {
  double dx0 = s0x[1] - s0x[0];
  double dx1 = s1x[1] - s1x[0];
  double dy0 = s0y[1] - s0y[0];
  double dy1 = s1y[1] - s1y[0];
  double p0 = dy1 * (s1x[1] - s0x[0]) - dx1 * (s1y[1] - s0y[0]);
  double p1 = dy1 * (s1x[1] - s0x[1]) - dx1 * (s1y[1] - s0y[1]);
  double p2 = dy0 * (s0x[1] - s1x[0]) - dx0 * (s0y[1] - s1y[0]);
  double p3 = dy0 * (s0x[1] - s1x[1]) - dx0 * (s0y[1] - s1y[1]);
  return ((p0 * p1 <= 0) && (p2 * p3 <= 0));
}

int crossesland(double stAx, double  stAy, double stBx, double stBy, double *LatDeg, double *LonDeg, int n) {
  int i, dn = 1;
  double s0x[2], s0y[2], s1x[2], s1y[2];
  
  /*if ((stAx == stBx) && (stAy == stBy))
    return 1;*/

  s0x[0] = stAx;
  s0x[1] = stBx;
  s0y[0] = -stAy;
  s0y[1] = -stBy;
  deg2point__(s0x,s0y,s0x,s0y,2,1000000);
  
  for (i = 0; i < n-dn; i = i + dn) { 
    s1x[0] = LatDeg[i];
    s1x[1] = LatDeg[i+dn];
    s1y[0] = LonDeg[i];
    s1y[1] = LonDeg[i+dn];
    deg2point__(s1x,s1y,s1x,s1y,2,1000000);
    if (1 == intersects(s0x,s0y,s1x,s1y)) {
      return 0;
    }
  }
  return 1;
}


int minDistance(double *dist, int *sptSet, int n) {
  double min = 100000000.0; /* Infinity */
  int v, min_index;
 
  for (v = 0; v < n; v++) {
    if ((sptSet[v] == 0) && (dist[v] <= min)) {
      min = dist[v];
      min_index = v;
    }
  }
  return min_index;
}

double dijkstra_dist_(double* graph, int n, int n_wayp, int src, int dest) {
    double d, * dist, INFTY = 10000000000.0;
    int* sptSet, i, j = 0, count, u, v;

    dist = (double*)malloc(n * sizeof(double));
    sptSet = (int*)calloc(n, sizeof(int));
    for (i = 0; i < n_wayp; i++)
        dist[i] = 2*INFTY; /* smaller than infinity above */
    for (i = n_wayp; i < n; i++)
        dist[i] = INFTY; /* smaller than infinity above */
    dist[src] = 0.0;
    for (count = n_wayp; count < n - 1; count++) {
        u = minDistance(dist, sptSet, n);
        sptSet[u] = 1;
        v = dest;
        if ((sptSet[v] == 0) && (graph[u + n * v] > 0) && (dist[u] < INFTY) && (dist[u] + graph[u + n * v] < dist[v])) {
            dist[v] = dist[u] + graph[u + n * v];
        }
        for (v = n_wayp; v < n; v++) {
            if ((sptSet[v] == 0) && (graph[u + n * v] > 0) && (dist[u] < INFTY) && (dist[u] + graph[u + n * v] < dist[v])) {
                dist[v] = dist[u] + graph[u + n * v];
            }
        }
    }
    d = dist[dest];
    free(dist);
    free(sptSet);
    return d;
}


int readMAP() {
  int i;
  
  FILE *file;
  float *landata;
  size_t fileSize, numElements;

  // Open the file
  file = fopen("island.bin", "rb");
  if (file == NULL) {
    perror("Error opening file");
    return -1;
  }
  // Determine the file size
  fseek(file, 0, SEEK_END);
  fileSize = ftell(file);
  rewind(file);

  // Calculate the number of elements
  numElements = fileSize / sizeof(float);

  // Allocate memory for the data
  landata = (float *)malloc(fileSize);
  if (landata == NULL) {
    perror("Memory allocation failed");
    fclose(file);
    return -1;
  }

  // Read data from the file
  fread(landata, sizeof(float), numElements, file);
  fclose(file);

  MAP[0].n = 1;
  MAP[0].N[0] = numElements / 2;
  MAP[0].LatDeg[0] = (double *) malloc (MAP[0].N[0]*sizeof(double));
  MAP[0].LonDeg[0] = (double *) malloc (MAP[0].N[0]*sizeof(double));
  if ( (MAP[0].LatDeg[0] == NULL) || (MAP[0].LonDeg[0] == NULL) ) {
    return -1;
  }
  for (i = 0; i < MAP[0].N[0]; i++) {
    MAP[0].LatDeg[0][i] = (double)landata[i];
    MAP[0].LonDeg[0][i] = (double)landata[MAP[0].N[0] + i];
  }
  MAP[0].MAXLON = -4;
  MAP[0].MAXLAT = 70;
  MAP[0].MINLAT = 60;
  MAP[0].MINLON = -32;
  return 0;
}

int createfeasiblelinkmatrix(PARAMS params) {
  int i, j, k;
  int m = params.SelectedSize, M = 2*params.SelectedSize;
  double x1, y1, x2, y2;
  int *F = params.FsbleLink;
  int n = MAP[iMAP].N[0];
  double *LatDeg = MAP[iMAP].LatDeg[0];
  double *LonDeg = MAP[iMAP].LonDeg[0];

  for (i = 0; i < M; i++)
    F[i+M*i] = 1;
  for (i = 0; i < m; i++) {
    for (j = i + 1; j < m; j++) {
       x1 = 180.0*params.LatLonRad[0][i] / PI;
       y1 = 180.0*params.LatLonRad[1][i] / PI;
       x2 = 180.0*params.LatLonRad[0][j] / PI;
       y2 = 180.0*params.LatLonRad[1][j] / PI;
       k = crossesland(x1, y1, x2, y2, LatDeg, LonDeg, n);
       F[(2*i)+M*(2*j)] = k; F[(2*j)+M*(2*i)] = k;
       x1 = 180.0*params.LatLonRad[0][i] / PI;
       y1 = 180.0*params.LatLonRad[1][i] / PI;
       x2 = 180.0*params.LatLonRad[2][j] / PI;
       y2 = 180.0*params.LatLonRad[3][j] / PI;
       k = crossesland(x1, y1, x2, y2, LatDeg, LonDeg, n);
       F[(2*i)+M*(2*j+1)] = k; F[(2*j+1)+M*(2*i)] = k;
       x1 = 180.0*params.LatLonRad[2][i] / PI;
       y1 = 180.0*params.LatLonRad[3][i] / PI;
       x2 = 180.0*params.LatLonRad[0][j] / PI;
       y2 = 180.0*params.LatLonRad[1][j] / PI;
       k = crossesland(x1, y1, x2, y2, LatDeg, LonDeg, n);
       F[(2*i+1)+M*(2*j)] = k; F[(2*j)+M*(2*i+1)] = k;
       x1 = 180.0*params.LatLonRad[2][i] / PI;
       y1 = 180.0*params.LatLonRad[3][i] / PI;
       x2 = 180.0*params.LatLonRad[2][j] / PI;
       y2 = 180.0*params.LatLonRad[3][j] / PI;
       k = crossesland(x1, y1, x2, y2, LatDeg, LonDeg, n);
       F[(2*i+1)+M*(2*j+1)] = k; F[(2*j+1)+M*(2*i+1)] = k;
    }
  }
  return 0;
}

void CreateDistanceMatrix(PARAMS params) {
  int i, j, k;
  int m = params.SelectedSize, M = 2*params.SelectedSize;
  int n_wayp = 2 * params.Size;
  double x1, y1, x2, y2, d;
  int *F = params.FsbleLink;
  double* D = params.DistMtrx;
  double* G = params.Graph;

  for (i = 0; i < M; i++)
    D[i+M*i] = 10000000000000000000000.0;
  for (i = 0; i < m; i++) {
    for (j = i + 1; j < m; j++) {
       x1 = params.LatLonRad[0][i];
       y1 = params.LatLonRad[1][i];
       x2 = params.LatLonRad[0][j];
       y2 = params.LatLonRad[1][j];
       d = arcdist(x1, y1, x2, y2);
       if (F[(2*i)+(2*j)*M] == 0)
         d += 100000.0;
       D[(2*i)+(2*j)*M] = d;
       D[(2*j)+(2*i)*M] = d;
       x1 = params.LatLonRad[0][i];
       y1 = params.LatLonRad[1][i];
       x2 = params.LatLonRad[2][j];
       y2 = params.LatLonRad[3][j];
       d = arcdist(x1, y1, x2, y2);
       if (F[(2*i)+(2*j+1)*M] == 0)
         d += 100000.0;
       D[(2*i)+(2*j+1)*M] = d;
       D[(2*j+1)+(2*i)*M] = d;
       x1 = params.LatLonRad[2][i];
       y1 = params.LatLonRad[3][i];
       x2 = params.LatLonRad[0][j];
       y2 = params.LatLonRad[1][j];
       d = arcdist(x1, y1, x2, y2);
       if (F[(2*i+1)+(2*j)*M] == 0)
         d += 100000.0;
       D[(2*i+1)+(2*j)*M] = d;
       D[(2*j)+(2*i+1)*M] = d;
       x1 = params.LatLonRad[2][i];
       y1 = params.LatLonRad[3][i];
       x2 = params.LatLonRad[2][j];
       y2 = params.LatLonRad[3][j];
       d = arcdist(x1, y1, x2, y2);
       if (F[(2*i+1)+(2*j+1)*M] == 0)
         d += 100000.0;
       D[(2*i+1)+(2*j+1)*M] = d;
       D[(2*j+1)+(2*i+1)*M] = d;
    }
  }
  /* copy the contents of the distance matrix to a graph */
  memcpy(G, D, M*M*sizeof(double));
  /* now adjust distance based on feasible search */
  /* note that if no feasible path is found the distance will be INFTY */
  for (i = 0; i < m; i++) {
    for (j = i + 1; j < m; j++) {
      if (F[(2*i)+(2*j)*M] == 0) {
        d = dijkstra_dist_(G, M, n_wayp, 2*i, 2*j);
        D[(2*i)+(2*j)*M] = d;
        D[(2*j)+(2*i)*M] = d;
      }
      if (F[(2*i)+(2*j+1)*M] == 0) {
        d = dijkstra_dist_(G, M, n_wayp, 2*i, 2*j+1);
        D[(2*i)+(2*j+1)*M] = d;
        D[(2*j+1)+(2*i)*M] = d;
      }
      if (F[(2*i+1)+(2*j)*M] == 0) {
        d = dijkstra_dist_(G, M, n_wayp, 2*i+1, 2*j);
        D[(2*i+1)+(2*j)*M] = d;
        D[(2*j)+(2*i+1)*M] = d;
      }
      if (F[(2*i+1)+(2*j+1)*M] == 0) {
        d = dijkstra_dist_(G, M, n_wayp, 2*i+1, 2*j+1);
        D[(2*i+1)+(2*j+1)*M] = d;
        D[(2*j+1)+(2*i+1)*M] = d;
      }
    }
  }
}


EXPORT int DistanceLink(double *DistrMtrx, int *FsbleMtrx, int *Type, double *LatLon[4], double *StartEnd, int Size, int SelectedSize) {

  int i;
  PARAMS params;
  params.SelectedSize = SelectedSize ;
  params.Type = Type;
  params.StartEnd = StartEnd;
  params.Size = Size; // What is this ?! with WAYP ?The Size-1 ? in params now includes startend at zero for LatLon
  params.DistMtrx = DistrMtrx;
  params.FsbleLink = FsbleMtrx;
  params.Graph = (double*)calloc(4 * SelectedSize * SelectedSize, sizeof(double));
  
  if (readMAP() != 0) {
    printf("Error: cound not read MAP!\n");
    return -1;
  }
  
  for (i=0; i<4; i++)
    params.LatLonRad[i] = LatLon[i];

  createfeasiblelinkmatrix(params);
  CreateDistanceMatrix(params);

  free(params.Graph);
  return 0;
}

EXPORT int dijkstra(double *d, int *path, double **graph, int n, int src, int dest) {
  double *dist, INFTY = 10000000000.0;
  int *sptSet, i, j=0, count, u, v, *nodes;

  dist = (double *) malloc(n*sizeof(double)); 
  sptSet = (int *) calloc(n, sizeof(int));
  nodes = (int *) calloc(n, sizeof(int));
  for (i = 0; i < n; i++)
    dist[i] = INFTY; /* smaller than infinity above */
  dist[src] = 0.0;
  for (count = 0; count < n - 1; count++) {
    u = minDistance(dist, sptSet, n);
    sptSet[u] = 1;
    for (v = 0; v < n; v++) {
      if ((sptSet[v]==0) && (graph[u][v] > 0) && (dist[u] < INFTY) && (dist[u] + graph[u][v] < dist[v])) {
        dist[v] = dist[u] + graph[u][v];
        nodes[v] = u;
      }
    }
  }
  nodes[src] = dest;
  i = dest;
  path[j++] = i;
  while (i != src) {
    i = nodes[i];
    path[j++] = i;
  }
  path[j] = -1; /* end of path */
  *d = dist[dest];
  free(dist);
  free(sptSet);
  free(nodes);
  return 0;
}
