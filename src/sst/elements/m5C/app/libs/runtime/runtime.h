// Copyright 2009-2015 Sandia Corporation. Under the terms
// of Contract DE-AC04-94AL85000 with Sandia Corporation, the U.S.
// Government retains certain rights in this software.
// 
// Copyright (c) 2009-2015, Sandia Corporation
// All rights reserved.
// 
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#ifndef _m5rt_h
#define _m5rt_h

typedef struct {
    int nid;
    int pid;
} cnos_nidpid_map_t;

#ifdef __cplusplus
extern "C" {
#endif

extern int cnos_get_rank( void );
extern int cnos_get_size( void );
extern int cnos_barrier( void );
extern int cnos_get_nidpid_map( cnos_nidpid_map_t** map );
#ifdef __cplusplus
}
#endif
 
#endif
