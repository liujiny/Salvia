// Optional Xbox FBNeo diagnostics. Normal builds do not write diagnostic logs.
#ifndef SALVIA_FBNEO_DIAGNOSTICS_H
#define SALVIA_FBNEO_DIAGNOSTICS_H
#ifndef SALVIA_FBNEO_DIAGNOSTICS
#define SALVIA_FBNEO_DIAGNOSTICS 0
#endif
#if SALVIA_FBNEO_DIAGNOSTICS != 0 && SALVIA_FBNEO_DIAGNOSTICS != 1
#error SALVIA_FBNEO_DIAGNOSTICS must be 0 or 1
#endif
#endif
