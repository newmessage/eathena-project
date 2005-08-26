// $Id: vending.h,v 1.2 2004/09/25 05:32:19 MouseJstr Exp $
#ifndef	_VENDING_H_
#define	_VENDING_H_

#include "map.h"

void vending_closevending(struct map_session_data &sd);
void vending_openvending(struct map_session_data &sd,unsigned short len,const char *message,int flag, unsigned char *buffer);
void vending_vendinglistreq(struct map_session_data &sd,unsigned long id);
void vending_purchasereq(struct map_session_data &sd,unsigned short len,unsigned long id, unsigned char *buffer);

#endif	// _VENDING_H_
