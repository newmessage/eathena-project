#ifndef _LOGIN_H_
#define _LOGIN_H_

#define MAX_SERVERS 30

#define LOGIN_CONF_NAME	"conf/login_athena.conf"
#define SQL_CONF_NAME "conf/inter_athena.conf"
#define LAN_CONF_NAME     "conf/lan_support.conf"

#define PASSWORDENC		3	// A definition is given when making an encryption password correspond.
							// It is 1 at the time of passwordencrypt.
							// It is made into 2 at the time of passwordencrypt2.
							// When it is made 3, it corresponds to both.

#define START_ACCOUNT_NUM	  2000000
#define END_ACCOUNT_NUM		100000000

struct mmo_account {
	char* userid;
	char* passwd;
	int passwdenc;
	long account_id;
	long login_id1;
	long login_id2;
	long char_id;
	char lastlogin[24];
	int sex;
	int version;	//Added by sirius for versioncheck
	unsigned char level;
};


#endif