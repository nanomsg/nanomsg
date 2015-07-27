/* 
 * File:   sockevt.h
 * Author: alexey
 *
 * Created on July 26, 2015, 10:05 PM
 */

#ifndef SOCKEVT_H
#define	SOCKEVT_H

#ifdef	__cplusplus
extern "C" {
#endif

#define NN_SOCKET_ADDED     1
#define NN_SOCKET_REMOVED   2    
    
/******************************************************************************/
/*  Socket events support.                                              */
/******************************************************************************/

struct nn_sock_evt {
    int type;
    unsigned long id;
};



#ifdef	__cplusplus
}
#endif

#endif	/* SOCKEVT_H */

