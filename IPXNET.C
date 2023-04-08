// ipxnet.c

#include <stdio.h>
#include <stdlib.h>
#include <dos.h>
#include <string.h>
#include <process.h>
#include <values.h>

#include "ipxnet.h"

/*
==========================================================================
===

                              IPX PACKET DRIVER

==========================================================================
===
*/

packet_t        packets[NUMPACKETS];

nodeadr_t      nodeadr[MAXNETNODES+1];  // first is local, last is broadcast

nodeadr_t      remoteadr;               // set by each GetPacket

localadr_t          localadr;           // set at startup

unsigned char req[12];
unsigned char resp[256];


extern int socketid;

void far (*IPX)(void);

long           localtime;          // for time stamp in packets
long           remotetime;

//===========================================================================

void LookupNets() {
	unsigned int x;
	printf("Checking Network Rechability:\n");
	printf("Local Network:%02x%02x%02x%02x\n",localadr.network[0],localadr.network[1],localadr.network[2],localadr.network[3]);
	for(x=0;x<8;x++) {
		if(!memcmp(&networks[x].network,&localadr.network,4)) {
			memset(&networks[x],0,sizeof(networks[x]));
			continue;
		}
		if(networks[x].network[0]==0 && networks[x].network[1]==0 && networks[x].network[2]==0 && networks[x].network[3]==0) continue;
		printf("Checking %02x%02x%02x%02x...",networks[x].network[0],networks[x].network[1],networks[x].network[2],networks[x].network[3]);

		memcpy(&req[0],&networks[x].network[0],4);
		memset(&req[4],0xFF,8);//destination node and socket

		_ES = FP_SEG(&req);
		_SI = FP_OFF(&req);
		_DI = FP_OFF(&networks[x].immaddr);
		_BX = 2;
		_AL = 0;
		asm push bp;
		IPX();
		asm pop bp;
		if(_AL) {
			memset(&networks[x],0,sizeof(networks[x]));
			printf("unreachable.\n");
		}
		else {
			printf("ok (%u)\n",_CX);//_CX is transport time (1/18th seconds per 576 bytes)
		}
	}
	printf("\n\n");
}


int OpenSocket(short socketNumber)
{
     _DX = socketNumber;
	 _BX = 0;
     _AL = 0;
     IPX();
     if(_AL)
	  Error ("OpenSocket: 0x%x", _AL);
	 return _DX;
}


void CloseSocket(short socketNumber)
{
     _DX = socketNumber;
     _BX = 1;
	 IPX();
}

void ListenForPacket(ECB *ecb)
{
     _SI = FP_OFF(ecb);
     _ES = FP_SEG(ecb);
     _BX = 4;
	 IPX();
     if(_AL)
          Error ("ListenForPacket: 0x%x", _AL);
}


void GetLocalAddress (void)
{
	 _SI = FP_OFF(&localadr);
	 _ES = FP_SEG(&localadr);
	 _BX = 9;
	 IPX();
}



/*
====================
=
= InitNetwork
=
====================
*/

void InitNetwork (void)
{
     int     i,j;

//
// get IPX function address
//
	 _AX = 0x7a00;
	 geninterrupt(0x2f);
     if(_AL != 0xff)
	  Error ("IPX not detected\n");
     IPX = MK_FP(_ES, _DI);


//
// allocate a socket for sending and receiving
//
     socketid = OpenSocket ( (socketid>>8) + ((socketid&255)<<8) );

     GetLocalAddress();

//
// set up several receiving ECBs
//
     memset (packets,0,NUMPACKETS*sizeof(packet_t));

     for (i=1 ; i<NUMPACKETS ; i++)
     {
          packets[i].ecb.ECBSocket = socketid;
          packets[i].ecb.FragmentCount = 1;
          packets[i].ecb.fAddress[0] = FP_OFF(&packets[i].ipx);
          packets[i].ecb.fAddress[1] = FP_SEG(&packets[i].ipx);
          packets[i].ecb.fSize = sizeof(packet_t)-sizeof(ECB);

	  ListenForPacket (&packets[i].ecb);
     }

//
// set up a sending ECB
//
     memset (&packets[0],0,sizeof(packets[0]));

     packets[0].ecb.ECBSocket = socketid;
     packets[0].ecb.FragmentCount = 2;
     packets[0].ecb.fAddress[0] = FP_OFF(&packets[0].ipx);
     packets[0].ecb.fAddress[1] = FP_SEG(&packets[0].ipx);
	 for (j=0 ; j<4 ; j++)
	  packets[0].ipx.dNetwork[j] = localadr.network[j];
     packets[0].ipx.dSocket[0] = socketid&255;
     packets[0].ipx.dSocket[1] = socketid>>8;
     packets[0].ecb.f2Address[0] = FP_OFF(&doomcom.data);
     packets[0].ecb.f2Address[1] = FP_SEG(&doomcom.data);

// known local node at 0
     for (i=0 ; i<6 ; i++)
	  nodeadr[0].node[i] = localadr.node[i];
     for(i=0;i<4;i++)
	nodeadr[0].network[i] = localadr.network[i];

// broadcast node at MAXNETNODES
     for (j=0 ; j<6 ; j++)
	  nodeadr[MAXNETNODES].node[j] = 0xff;
     for (j=0 ; j<4 ; j++)
	nodeadr[MAXNETNODES].network[j]=localadr.network[j];

}


/*
====================
=
= ShutdownNetwork
=
====================
*/

void ShutdownNetwork (void)
{
	if (IPX)
			CloseSocket (socketid);
}


/*
==============
=
= SendPacket
=
= A destination of MAXNETNODES is a broadcast
==============
*/

void SendPacket (int destination,char *destnet) {
	int             j,i;

	// set the time
	packets[0].time = localtime;

	// set the address
	memcpy(&packets[0].ipx.dNode,nodeadr[destination].node,6);
	if(destnet) {
		memcpy(&packets[0].ipx.dNetwork,destnet,4);
	}
	else {
		memcpy(&packets[0].ipx.dNetwork,nodeadr[destination].network,4);
	}

	if(memcmp(&packets[0].ipx.dNetwork,&localadr.network,4)) {
		memset(&packets[0].ecb.ImmediateAddress,0xFF,6);//fallback
		for(i=0;i<8;i++) {
			if(!memcmp(&networks[i].network,&packets[0].ipx.dNetwork,4)) {
					memcpy(&packets[0].ecb.ImmediateAddress,&networks[i].immaddr,6);
					break;
			}
		}
	}
	else {//for localnet
		memcpy(&packets[0].ecb.ImmediateAddress,nodeadr[destination].node,6);
	}


// set the length (ipx + time + datalength)
	 packets[0].ecb.fSize = sizeof(IPXPacket) + 4;
	 packets[0].ecb.f2Size = doomcom.datalength + 4;

// send the packet
	 _SI = FP_OFF(&packets[0]);
	 _ES = FP_SEG(&packets[0]);
	 _BX = 3;
	 IPX();
     if(_AL)
	  Error("SendPacket: 0x%x", _AL);

     while(packets[0].ecb.InUseFlag != 0)
     {
	  // IPX Relinquish Control - polled drivers MUST have this here!
	  _BX = 10;
	  IPX();
     }
}


unsigned short ShortSwap (unsigned short i)
{
     return ((i&255)<<8) + ((i>>8)&255);
}

/*
==============
=
= GetPacket
=
= Returns false if no packet is waiting
=
==============
*/

int GetPacket (void) {
	int             packetnum;
	int             i, j;
	long           besttic;
	packet_t       *packet;

	// if multiple packets are waiting, return them in order by time

	besttic = MAXLONG;
	packetnum = -1;
	doomcom.remotenode = -1;

	for ( i = 1 ; i < NUMPACKETS ; i++) {
		if(packets[i].ecb.InUseFlag) {
			continue;
		}
		if (packets[i].time < besttic) {
			besttic = packets[i].time;
			packetnum = i;
		}
	}

	if (besttic == MAXLONG) {
		return 0;                           // no packets
	}
	packet = &packets[packetnum];

	if (besttic == -1 && localtime != -1) {
		ListenForPacket (&packet->ecb);
		return 0;            	// setup broadcast from other game
	}

	remotetime = besttic;

	//
	// got a good packet
	//
	if (packet->ecb.CompletionCode) {
		Error ("GetPacket: ecb.ComletionCode = 0x%x",packet->ecb.CompletionCode);
	}

	//ensure if it is from a network we are interested in
	if(memcmp(packet->ipx.sNetwork,&localadr.network,4)) {
		for(i=0;i<8;i++) {
			if(!memcmp(&networks[i].network,&packet->ipx.sNetwork,4)) {
					break;
			}
		}
		if(i==8) {
			ListenForPacket (&packet->ecb);
			return 0;
		}
	}



	// set remoteadr to the sender of the packet
	memcpy (&remoteadr, packet->ipx.sNetwork, sizeof(remoteadr));
	for (i=0 ; i<doomcom.numnodes ; i++) {
		if (!memcmp(&remoteadr, &nodeadr[i], sizeof(remoteadr))) break;
	}
	if (i < doomcom.numnodes) {
		doomcom.remotenode = i;
	}
	else {
		if (localtime != -1) {    // this really shouldn't happen
			ListenForPacket (&packet->ecb);
			return 0;
		}
	}
	// copy out the data
	doomcom.datalength = ShortSwap(packet->ipx.PacketLength) - 38;
	memcpy (&doomcom.data, &packet->data, doomcom.datalength);

	// repost the ECB
	ListenForPacket (&packet->ecb);

	return 1;
}

