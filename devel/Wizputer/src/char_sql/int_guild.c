//
// original code from athena
// SQL conversion by hack
//

#include "base.h"
#include "strlib.h"
#include "socket.h"
#include "mmo.h"
#include "malloc.h"
#include "version.h"
#include "db.h"
#include "mmo.h"
#include "utils.h"
#include "showmsg.h"
#include "../common/dbaccess.h"

#include "char.h"
#include "int_storage.h"
#include "inter.h"
#include "int_guild.h"
#include "int_storage.h"

static struct dbt *guild_db_;
static struct dbt *castle_db_;


static int guild_newid=10000;

static int guild_exp[100];

int mapif_parse_GuildLeave(int fd,int guild_id,unsigned long account_id,unsigned long char_id,int flag,const char *mes);
int mapif_guild_broken(unsigned long guild_id,int flag);
int guild_check_empty(struct guild *g);
int guild_calcinfo(struct guild *g);
int mapif_guild_basicinfochanged(unsigned long guild_id,int type, unsigned long data);
int mapif_guild_info(int fd,struct guild *g);
int guild_break_sub(void *key,void *data,va_list ap);


int _erase_guild(void *key, void *data, va_list ap)
{
    unsigned long guild_id = (unsigned long )va_arg(ap, int);
    struct guild_castle * castle = (struct guild_castle *) data;
    if (castle->guild_id == guild_id)
	{
        aFree(castle);
        db_erase(castle_db_, key);
    }
    return 0;
}

// Save guild into sql
int inter_guild_tosql(struct guild *g,int flag)
{
	// 1 `guild` (`guild_id`, `name`,`master`,`guild_lv`,`connect_member`,`max_member`,`average_lv`,`exp`,`next_exp`,`skill_point`,`castle_id`,`mes1`,`mes2`,`emblem_len`,`emblem_id`,`emblem_data`)
	// 2 `guild_member` (`guild_id`,`account_id`,`char_id`,`hair`,`hair_color`,`gender`,`class`,`lv`,`exp`,`exp_payper`,`online`,`position`,`rsv1`,`rsv2`,`name`)
	// 4 `guild_position` (`guild_id`,`position`,`name`,`mode`,`exp_mode`)
	// 8 `guild_alliance` (`guild_id`,`opposition`,`alliance_id`,`name`)
	// 16 `guild_expulsion` (`guild_id`,`name`,`mes`,`acc`,`account_id`,`rsv1`,`rsv2`,`rsv3`)
	// 32 `guild_skill` (`guild_id`,`id`,`lv`)

	char t_name[128],t_master[32],t_mes1[128],t_mes2[256],t_member[32],t_position[32],t_alliance[32];  // temporay storage for str convertion;
	char t_ename[32],t_emes[64];
	char emblem_data[4096];
	int i=0;
	int guild_exist=0,guild_member=0,guild_online_member=0;

	if (g->guild_id<=0) return -1;

	ShowMessage("("CL_BT_MAGENTA"%d"CL_NORM")  Request save guild -(flag 0x%x) ",g->guild_id, flag);

	jstrescapecpy(t_name, g->name);

	//ShowMessage("- Check if guild %d exists\n",g->guild_id);
	sprintf(tmp_sql, "SELECT count(*) FROM `%s` WHERE `guild_id`='%ld'",guild_db,g->guild_id);
	if(mysql_SendQuery(mysql_handle, tmp_sql) ) {
		ShowMessage("DB server Error (delete `guild`)- %s\n", mysql_error(mysql_handle) );
	}
	sql_res = mysql_store_result(mysql_handle) ;
	if (sql_res!=NULL && mysql_num_rows(sql_res)>0) {
		sql_row = mysql_fetch_row(sql_res);
		guild_exist =  atoi(sql_row[0]);
		//ShowMessage("- Check if guild %d exists : %s\n",g->guild_id,((guild_exist==0)?"No":"Yes"));
	}
	mysql_free_result(sql_res) ; //resource free

	if (guild_exist >0){
		// Check members in party
		sprintf(tmp_sql,"SELECT count(*) FROM `%s` WHERE `guild_id`='%ld'",guild_member_db, g->guild_id);
		if(mysql_SendQuery(mysql_handle, tmp_sql) ) {
			ShowMessage("DB server Error - %s\n", mysql_error(mysql_handle) );
			return -1;
		}
		sql_res = mysql_store_result(mysql_handle) ;
		if (sql_res!=NULL && mysql_num_rows(sql_res)>0) {
			sql_row = mysql_fetch_row(sql_res);

			guild_member =  atoi (sql_row[0]);
		//	ShowMessage("- Check members in guild %d : %d \n",g->guild_id,guild_member);

		}
		mysql_free_result(sql_res) ; //resource free

		// Delete old guild from sql
		if (flag&1||guild_member==0){
		//	ShowMessage("- Delete guild %d from guild\n",g->guild_id);
			sprintf(tmp_sql, "DELETE FROM `%s` WHERE `guild_id`='%ld'",guild_db, g->guild_id);
			if(mysql_SendQuery(mysql_handle, tmp_sql) ) {
				ShowMessage("DB server Error (delete `guild`)- %s\n", mysql_error(mysql_handle) );
			}
		}
		if (flag&2||guild_member==0){
		//	ShowMessage("- Delete guild %ld from guild_member\n",g->guild_id);
			sprintf(tmp_sql, "DELETE FROM `%s` WHERE `guild_id`='%ld'",guild_member_db, g->guild_id);
			if(mysql_SendQuery(mysql_handle, tmp_sql) ) {
				ShowMessage("DB server Error (delete `guild_member`)- %s\n", mysql_error(mysql_handle) );
			}
			sprintf(tmp_sql, "UPDATE `%s` SET `guild_id`='0' WHERE `guild_id`='%ld'",char_db, g->guild_id);
			if(mysql_SendQuery(mysql_handle, tmp_sql) ) {
				ShowMessage("DB server Error (update `char`)- %s\n", mysql_error(mysql_handle) );
			}
		}
		if (flag&32||guild_member==0){
		//	ShowMessage("- Delete guild %ld from guild_skill\n",g->guild_id);
			sprintf(tmp_sql, "DELETE FROM `%s` WHERE `guild_id`='%ld'",guild_skill_db, g->guild_id);
			if(mysql_SendQuery(mysql_handle, tmp_sql) ) {
				ShowMessage("DB server Error (delete `guild_skill`)- %s\n", mysql_error(mysql_handle) );
			}
		}
		if (flag&4||guild_member==0){
		//	ShowMessage("- Delete guild %ld from guild_position\n",g->guild_id);
			sprintf(tmp_sql, "DELETE FROM `%s` WHERE `guild_id`='%ld'",guild_position_db, g->guild_id);
			if(mysql_SendQuery(mysql_handle, tmp_sql) ) {
				ShowMessage("DB server Error (delete `guild_position`)- %s\n", mysql_error(mysql_handle) );
			}
		}
		if (flag&16||guild_member==0){
		//	ShowMessage("- Delete guild %ld from guild_expulsion\n",g->guild_id);
			sprintf(tmp_sql, "DELETE FROM `%s` WHERE `guild_id`='%ld'",guild_expulsion_db, g->guild_id);
			if(mysql_SendQuery(mysql_handle, tmp_sql) ) {
				ShowMessage("DB server Error (delete `guild_expulsion`)- %s\n", mysql_error(mysql_handle) );
			}
		}
		if (flag&8||guild_member==0){
		//	ShowMessage("- Delete guild %ld from guild_alliance\n",g->guild_id);
			sprintf(tmp_sql, "DELETE FROM `%s` WHERE `guild_id`='%ld' OR `alliance_id`='%ld'",guild_alliance_db, g->guild_id,g->guild_id);
			if(mysql_SendQuery(mysql_handle, tmp_sql) ) {
				ShowMessage("DB server Error (delete `guild_alliance`)- %s\n", mysql_error(mysql_handle) );
			}
		}
		if (flag&2||guild_member==0){
		//	ShowMessage("- Delete guild %ld from char\n",g->guild_id);
			sprintf(tmp_sql, "UPDATE `%s` SET `guild_id`='0' WHERE `guild_id`='%ld'",char_db, g->guild_id);
			if(mysql_SendQuery(mysql_handle, tmp_sql) ) {
				ShowMessage("DB server Error (delete `guild_alliance`)- %s\n", mysql_error(mysql_handle) );
			}
		}
		if (guild_member==0){
		//	ShowMessage("- Delete guild %ld from guild_castle\n",g->guild_id);
			sprintf(tmp_sql, "DELETE FROM `%s` WHERE `guild_id`='%ld'",guild_castle_db, g->guild_id);
			if(mysql_SendQuery(mysql_handle, tmp_sql) ) {
				ShowMessage("DB server Error (delete `guild_castle`)- %s\n", mysql_error(mysql_handle) );
			}
			db_foreach(castle_db_, _erase_guild, g->guild_id);
		}
	}

	guild_online_member = 0;
	i=0;
	while (i<g->max_member) {
		if (g->member[i].account_id>0) guild_online_member++;
		i++;
	}

	// No member in guild , no need to create it in sql
	if (guild_member <= 0 && guild_online_member <=0) {
		inter_guild_storage_delete(g->guild_id);
		ShowMessage("No member in guild %d , break it! \n",g->guild_id);
		return -2;
	}

	// Insert new guild to sqlserver
	if (flag&1||guild_member==0){
		int len=0;
		//ShowMessage("- Insert guild %d to guild\n",g->guild_id);
		for(i=0;i<g->emblem_len;i++){
			len+=sprintf(emblem_data+len,"%02x",(unsigned char)(g->emblem_data[i]));
			//ShowMessage("%02x",(unsigned char)(g->emblem_data[i]));
		}
                emblem_data[len] = '\0';
		//ShowMessage("- emblem_len = %d \n",g->emblem_len);
		sprintf(tmp_sql,"INSERT INTO `%s` "
			"(`guild_id`, `name`,`master`,`guild_lv`,`connect_member`,`max_member`,`average_lv`,`exp`,`next_exp`,`skill_point`,`castle_id`,`mes1`,`mes2`,`emblem_len`,`emblem_id`,`emblem_data`) "
			"VALUES ('%ld', '%s', '%s', '%d', '%d', '%d', '%d', '%ld', '%ld', '%d', '%d', '%s', '%s', '%d', '%ld', '%s')",
			guild_db, g->guild_id,t_name,jstrescapecpy(t_master,g->master),
			g->guild_lv,g->connect_member,g->max_member,g->average_lv,g->exp,g->next_exp,g->skill_point,g->castle_id,
			jstrescapecpy(t_mes1,g->mes1),jstrescapecpy(t_mes2,g->mes2),g->emblem_len,g->emblem_id,emblem_data);
		//ShowMessage(" %s\n",tmp_sql);
		if(mysql_SendQuery(mysql_handle, tmp_sql) ) {
			ShowMessage("DB server Error (insert `guild`)- %s\n", mysql_error(mysql_handle) );
		}
	}

	if (flag&2||guild_member==0){
	  struct StringBuf sbuf;
	  struct StringBuf sbuf2;
	  int first = 1;
	  StringBuf_Init(&sbuf2);
	  StringBuf_Init(&sbuf);

	  StringBuf_Printf(&sbuf,"REPLACE `%s` (`guild_id`,`account_id`,`char_id`,`hair`,`hair_color`,`gender`,`class`,`lv`,`exp`,`exp_payper`,`online`,`position`,`rsv1`,`rsv2`,`name`) VALUES \n", guild_member_db);

	  StringBuf_Printf(&sbuf2, "UPDATE `%s` SET `guild_id`='%d' WHERE  `char_id` IN (",char_db, g->guild_id);

	  //ShowMessage("- Insert guild %d to guild_member\n",g->guild_id);
	  for(i=0;i<g->max_member;i++){
	    if (g->member[i].account_id>0){
	      struct guild_member *m = &g->member[i];
	      if (first == 0) {
		StringBuf_Printf(&sbuf , ", ");
		StringBuf_Printf(&sbuf2, ", ");
	      } else
		first = 0;
	      StringBuf_Printf(&sbuf, "('%d','%d','%d','%d','%d', '%d','%d','%d','%d','%d','%d','%d','%d','%d','%s')\n",
			       g->guild_id,
			       m->account_id,m->char_id,
			       m->hair,m->hair_color,m->gender,
			       m->class_,m->lv,m->exp,m->exp_payper,m->online,m->position,
			       0,0,
			       jstrescapecpy(t_member,m->name));

	      StringBuf_Printf(&sbuf2, "'%d'", m->char_id);
	    }
	  }
	  StringBuf_Printf(&sbuf2,")");

	  if(mysql_SendQuery(mysql_handle, StringBuf_Value(&sbuf)))
	    ShowMessage("DB server Error - %s\n", mysql_error(mysql_handle) );

	  if(mysql_SendQuery(mysql_handle, StringBuf_Value(&sbuf2)))
	    ShowMessage("DB server Error - %s\n", mysql_error(mysql_handle) );

	  StringBuf_Destroy(&sbuf2);
	  StringBuf_Destroy(&sbuf);
	}

	if (flag&4||guild_member==0){
		//ShowMessage("- Insert guild %d to guild_position\n",g->guild_id);
		for(i=0;i<MAX_GUILDPOSITION;i++){
			struct guild_position *p = &g->position[i];
			sprintf(tmp_sql,"INSERT INTO `%s` (`guild_id`,`position`,`name`,`mode`,`exp_mode`) VALUES ('%ld','%d', '%s','%ld','%ld')",
				guild_position_db, g->guild_id, i, jstrescapecpy(t_position,p->name),p->mode,p->exp_mode);
			//ShowMessage(" %s\n",tmp_sql);
			if(mysql_SendQuery(mysql_handle, tmp_sql) ) {
				ShowMessage("DB server Error (insert `guild_position`)- %s\n", mysql_error(mysql_handle) );
			}
		}
	}

	if (flag&8||guild_member==0){
		//ShowMessage("- Insert guild %d to guild_alliance\n",g->guild_id);
		for(i=0;i<MAX_GUILDALLIANCE;i++){
			struct guild_alliance *a=&g->alliance[i];
			if(a->guild_id>0){
				sprintf(tmp_sql,"INSERT INTO `%s` (`guild_id`,`opposition`,`alliance_id`,`name`) "
					"VALUES ('%ld','%ld','%ld','%s')",
					guild_alliance_db, g->guild_id,a->opposition,a->guild_id,jstrescapecpy(t_alliance,a->name));
				//ShowMessage(" %s\n",tmp_sql);
				if(mysql_SendQuery(mysql_handle, tmp_sql) ) {
					ShowMessage("DB server Error (insert `guild_alliance`)- %s\n", mysql_error(mysql_handle) );
				}
				sprintf(tmp_sql,"INSERT INTO `%s` (`guild_id`,`opposition`,`alliance_id`,`name`) "
					"VALUES ('%ld','%ld','%ld','%s')",
					guild_alliance_db, a->guild_id,a->opposition,g->guild_id,t_name);
				//ShowMessage(" %s\n",tmp_sql);
				if(mysql_SendQuery(mysql_handle, tmp_sql) ) {
					ShowMessage("DB server Error (insert `guild_alliance`)- %s\n", mysql_error(mysql_handle) );
				}
			}
		}
	}

	if (flag&16||guild_member==0){
		//ShowMessage("- Insert guild %d to guild_expulsion\n",g->guild_id);
		for(i=0;i<MAX_GUILDEXPLUSION;i++){
			struct guild_explusion *e=&g->explusion[i];
			if(e->account_id>0){
				sprintf(tmp_sql,"INSERT INTO `%s` (`guild_id`,`name`,`mes`,`acc`,`account_id`,`rsv1`,`rsv2`,`rsv3`) "
					"VALUES ('%ld','%s','%s','%s','%ld','%ld','%ld','%ld')",
					guild_expulsion_db, g->guild_id,
					jstrescapecpy(t_ename,e->name),jstrescapecpy(t_emes,e->mes),e->acc,e->account_id,e->rsv1,e->rsv2,e->rsv3 );
				//ShowMessage(" %s\n",tmp_sql);
				if(mysql_SendQuery(mysql_handle, tmp_sql) ) {
					ShowMessage("DB server Error (insert `guild_expulsion`)- %s\n", mysql_error(mysql_handle) );
				}
			}
		}
	}

	if (flag&32||guild_member==0){
		//ShowMessage("- Insert guild %d to guild_skill\n",g->guild_id);
		for(i=0;i<MAX_GUILDSKILL;i++){
			if (g->skill[i].id>0 && g->skill[i].lv>0){
				sprintf(tmp_sql,"INSERT INTO `%s` (`guild_id`,`id`,`lv`) VALUES ('%ld','%d','%d')",
					guild_skill_db, g->guild_id,g->skill[i].id,g->skill[i].lv);
				//ShowMessage("%s\n",tmp_sql);
				if(mysql_SendQuery(mysql_handle, tmp_sql) ) {
					ShowMessage("DB server Error (insert `guild_skill`)- %s\n", mysql_error(mysql_handle) );
				}
			}
		}
	}

	ShowMessage("Save guild done\n");
	return 0;
}

// Read guild from sql
struct guild * inter_guild_fromsql(int guild_id)
{
	int i;
	char emblem_data[4096];
	char *pstr;
	struct guild *g;

	if (guild_id<=0) return 0;

	g = (struct guild*)numdb_search(guild_db_,guild_id);
	if (g != NULL)
		return g;

	g = (struct guild*)aCalloc(1,sizeof(struct guild));

//	ShowMessage("Retrieve guild information from sql ......\n");
//	ShowMessage("- Read guild %d from sql \n",guild_id);

	sprintf(tmp_sql,"SELECT `guild_id`, `name`,`master`,`guild_lv`,`connect_member`,`max_member`,`average_lv`,`exp`,`next_exp`,`skill_point`,`castle_id`,`mes1`,`mes2`,`emblem_len`,`emblem_id`,`emblem_data` "
		"FROM `%s` WHERE `guild_id`='%d'",guild_db, guild_id);
	//ShowMessage("  %s\n",tmp_sql);
	if(mysql_SendQuery(mysql_handle, tmp_sql) ) {
		ShowMessage("DB server Error (select `guild`)- %s\n", mysql_error(mysql_handle) );
		aFree(g);
		return 0;
	}

	sql_res = mysql_store_result(mysql_handle) ;
	if (sql_res!=NULL && mysql_num_rows(sql_res)>0) {
		sql_row = mysql_fetch_row(sql_res);
		if (sql_row==NULL) {
			mysql_free_result(sql_res);
			aFree(g);
			return 0;
		}

		g->guild_id=atoi(sql_row[0]);
		safestrcpy(g->name,sql_row[1],24);
		safestrcpy(g->master,sql_row[2],24);
		g->guild_lv=atoi(sql_row[3]);
		g->connect_member=atoi(sql_row[4]);
                if (atoi(sql_row[5]) > MAX_GUILD) // Fix reduction of MAX_GUILD [PoW]
                        g->max_member = MAX_GUILD;
                else
                        g->max_member = atoi(sql_row[5]);
		g->average_lv=atoi(sql_row[6]);
		g->exp=atoi(sql_row[7]);
		g->next_exp=atoi(sql_row[8]);
		g->skill_point=atoi(sql_row[9]);
		g->castle_id=atoi(sql_row[10]);
		safestrcpy(g->mes1,sql_row[11],60);
		safestrcpy(g->mes2,sql_row[12],120);
		g->emblem_len=atoi(sql_row[13]);
		g->emblem_id=atoi(sql_row[14]);
		safestrcpy(emblem_data,sql_row[15],4096);
		for(i=0,pstr=emblem_data;i<g->emblem_len;i++,pstr+=2)
		{
			int c1=pstr[0],c2=pstr[1],x1=0,x2=0;
			if(c1>='0' && c1<='9')x1=c1-'0';
			if(c1>='a' && c1<='f')x1=c1-'a'+10;
			if(c1>='A' && c1<='F')x1=c1-'A'+10;

			if(c2>='0' && c2<='9')x2=c2-'0';
			if(c2>='a' && c2<='f')x2=c2-'a'+10;
			if(c2>='A' && c2<='F')x2=c2-'A'+10;
			g->emblem_data[i]=(x1<<4)|x2;
		}
	}
	mysql_free_result(sql_res);

	//ShowMessage("- Read guild_member %d from sql \n",guild_id);
	sprintf(tmp_sql,"SELECT `guild_id`,`account_id`,`char_id`,`hair`,`hair_color`,`gender`,`class`,`lv`,`exp`,`exp_payper`,`online`,`position`,`rsv1`,`rsv2`,`name` "
		"FROM `%s` WHERE `guild_id`='%d'  ORDER BY `position`", guild_member_db, guild_id);
	//ShowMessage("  %s\n",tmp_sql);
	if(mysql_SendQuery(mysql_handle, tmp_sql) ) {
		ShowMessage("DB server Error (select `guild_member`)- %s\n", mysql_error(mysql_handle) );
		aFree(g);
		return 0;
	}
	sql_res = mysql_store_result(mysql_handle) ;
	if (sql_res!=NULL && mysql_num_rows(sql_res)>0) {
		int i;
		for(i=0;((sql_row = mysql_fetch_row(sql_res))&&i<g->max_member);i++){
			struct guild_member *m = &g->member[i];
			m->account_id=atoi(sql_row[1]);
			m->char_id=atoi(sql_row[2]);
			m->hair=atoi(sql_row[3]);
			m->hair_color=atoi(sql_row[4]);
			m->gender=atoi(sql_row[5]);
			m->class_=atoi(sql_row[6]);
			m->lv=atoi(sql_row[7]);
			m->exp=atoi(sql_row[8]);
			m->exp_payper=atoi(sql_row[9]);
			m->online=atoi(sql_row[10]);
			if (atoi(sql_row[11]) >= MAX_GUILDPOSITION) // Fix reduction of MAX_GUILDPOSITION [PoW]
				m->position = MAX_GUILDPOSITION - 1;
			else
				m->position = atoi(sql_row[11]);
			safestrcpy(m->name,sql_row[14],24);
		}
	}
	mysql_free_result(sql_res);

	//ShowMessage("- Read guild_position %d from sql \n",guild_id);
	sprintf(tmp_sql,"SELECT `guild_id`,`position`,`name`,`mode`,`exp_mode` FROM `%s` WHERE `guild_id`='%d'",guild_position_db, guild_id);
	//ShowMessage("  %s\n",tmp_sql);
	if(mysql_SendQuery(mysql_handle, tmp_sql) ) {
		ShowMessage("DB server Error (select `guild_position`)- %s\n", mysql_error(mysql_handle) );
		aFree(g);
		return 0;
	}
	sql_res = mysql_store_result(mysql_handle) ;
	if (sql_res!=NULL && mysql_num_rows(sql_res)>0) {
		int i;
		for(i=0;((sql_row = mysql_fetch_row(sql_res))&&i<MAX_GUILDPOSITION);i++){
			int position = atoi(sql_row[1]);
			struct guild_position *p = &g->position[position];
			safestrcpy(p->name,sql_row[2],24);
			p->mode=atoi(sql_row[3]);
			p->exp_mode=atoi(sql_row[4]);
		}
	}
	mysql_free_result(sql_res);

	//ShowMessage("- Read guild_alliance %d from sql \n",guild_id);
	sprintf(tmp_sql,"SELECT `guild_id`,`opposition`,`alliance_id`,`name` FROM `%s` WHERE `guild_id`='%d'",guild_alliance_db, guild_id);
	if(mysql_SendQuery(mysql_handle, tmp_sql) ) {
		ShowMessage("DB server Error (select `guild_alliance`)- %s\n", mysql_error(mysql_handle) );
		aFree(g);
		return 0;
	}
	sql_res = mysql_store_result(mysql_handle) ;
	if (sql_res!=NULL && mysql_num_rows(sql_res)>0) {
		int i;
		for(i=0;((sql_row = mysql_fetch_row(sql_res))&&i<MAX_GUILDALLIANCE);i++){
			struct guild_alliance *a = &g->alliance[i];
			a->opposition=atoi(sql_row[1]);
			a->guild_id=atoi(sql_row[2]);
			safestrcpy(a->name,sql_row[3],24);
		}
	}
	mysql_free_result(sql_res);

	//ShowMessage("- Read guild_expulsion %d from sql \n",guild_id);
	sprintf(tmp_sql,"SELECT `guild_id`,`name`,`mes`,`acc`,`account_id`,`rsv1`,`rsv2`,`rsv3` FROM `%s` WHERE `guild_id`='%d'",guild_expulsion_db, guild_id);
	if(mysql_SendQuery(mysql_handle, tmp_sql) ) {
		ShowMessage("DB server Error (select `guild_expulsion`)- %s\n", mysql_error(mysql_handle) );
		aFree(g);
		return 0;
	}
	sql_res = mysql_store_result(mysql_handle) ;
	if (sql_res!=NULL && mysql_num_rows(sql_res)>0) {
		int i;
		for(i=0;((sql_row = mysql_fetch_row(sql_res))&&i<MAX_GUILDEXPLUSION);i++){
			struct guild_explusion *e = &g->explusion[i];

			safestrcpy(e->name,sql_row[1],24);
			safestrcpy(e->mes,sql_row[2],40);
			safestrcpy(e->acc,sql_row[3],24);
			e->account_id=atoi(sql_row[4]);
			e->rsv1=atoi(sql_row[5]);
			e->rsv2=atoi(sql_row[6]);
			e->rsv3=atoi(sql_row[7]);

		}
	}
	mysql_free_result(sql_res);

	//ShowMessage("- Read guild_skill %d from sql \n",guild_id);
	sprintf(tmp_sql,"SELECT `guild_id`,`id`,`lv` FROM `%s` WHERE `guild_id`='%d' ORDER BY `id`",guild_skill_db, guild_id);
	if(mysql_SendQuery(mysql_handle, tmp_sql) ) {
		ShowMessage("DB server Error (select `guild_skill`)- %s\n", mysql_error(mysql_handle) );
		aFree(g);
		return 0;
	}

	for(i = 0; i < MAX_GUILDSKILL; i++)
	{	//Skill IDs must always be initialized. [Skotlex]
		g->skill[i].id = i + GD_SKILLBASE;
	}

	sql_res = mysql_store_result(mysql_handle) ;
	if (sql_res!=NULL && mysql_num_rows(sql_res)>0)
	{
		while ((sql_row = mysql_fetch_row(sql_res)))
		{	//I know this seems ridiculous, but the skills HAVE to be placed on their 'correct' array slot or things break x.x [Skotlex]
			int id = atoi(sql_row[1])-GD_SKILLBASE;
			if (id >= 0 && id < MAX_GUILDSKILL)
				g->skill[id].lv=atoi(sql_row[2]);
		}
	}


	mysql_free_result(sql_res);
//	ShowMessage("Successfully retrieve guild information from sql!\n");
	numdb_insert(guild_db_, guild_id,g);
	return g;
}

int _set_guild_castle(void *key, void *data, va_list ap) {
    unsigned short castle_id = (unsigned short)va_arg(ap, int);
    unsigned long guild_id   = va_arg(ap, unsigned long);
    struct guild * g = (struct guild *) data;

    if (g->castle_id == castle_id)
        g->castle_id = 0xFFFF;
    if (g->guild_id == guild_id)
        g->castle_id = castle_id;
    return 0;
}

int inter_guildcastle_tosql(struct guild_castle *gc)
{
	struct guild_castle *gcopy;
	if(gc == NULL || gc->castle_id == 0xFFFF )
		return 0;

	gcopy = (struct guild_castle *)numdb_search(castle_db_,gc->castle_id);
	if (gcopy == NULL) {
		gcopy = (struct guild_castle *) aMalloc(sizeof(struct guild_castle));
		numdb_insert(castle_db_, gc->castle_id, gcopy);
	} else if ((gc->guild_id  == gcopy->guild_id ) && (  gc->economy  == gcopy->economy ) && ( gc->defense  == gcopy->defense ) && ( gc->triggerE  == gcopy->triggerE ) && ( gc->triggerD  == gcopy->triggerD ) && ( gc->nextTime  == gcopy->nextTime ) && ( gc->payTime  == gcopy->payTime ) && ( gc->createTime  == gcopy->createTime ) && ( gc->visibleC  == gcopy->visibleC ) && ( gc->visibleG0  == gcopy->visibleG0 ) && ( gc->visibleG1  == gcopy->visibleG1 ) && ( gc->visibleG2  == gcopy->visibleG2 ) && ( gc->visibleG3  == gcopy->visibleG3 ) && ( gc->visibleG4  == gcopy->visibleG4 ) && ( gc->visibleG5  == gcopy->visibleG5 ) && ( gc->visibleG6  == gcopy->visibleG6 ) && ( gc->visibleG7  == gcopy->visibleG7 ) && ( gc->Ghp0  == gcopy->Ghp0 ) && ( gc->Ghp1  == gcopy->Ghp1 ) && ( gc->Ghp2  == gcopy->Ghp2 ) && ( gc->Ghp3  == gcopy->Ghp3 ) && ( gc->Ghp4  == gcopy->Ghp4 ) && ( gc->Ghp5  == gcopy->Ghp5 ) && ( gc->Ghp6  == gcopy->Ghp6 ) && ( gc->Ghp7 == gcopy->Ghp7 )) {
		//if the castle data hasn't been changed, then we don't write it into SQL
		return 0;
	}
	ShowMessage("[Guild Castle %02i]: Save... ->SQL\n",gc->castle_id);

	memcpy(gcopy, gc, sizeof(struct guild_castle));
	sprintf(tmp_sql,"REPLACE INTO `%s` "
		"(`castle_id`, `guild_id`, `economy`, `defense`, `triggerE`, `triggerD`, `nextTime`, `payTime`, `createTime`,"
		"`visibleC`, `visibleG0`, `visibleG1`, `visibleG2`, `visibleG3`, `visibleG4`, `visibleG5`, `visibleG6`, `visibleG7`,"
		"`Ghp0`, `Ghp1`, `Ghp2`, `Ghp3`, `Ghp4`, `Ghp5`, `Ghp6`, `Ghp7`)"
		"VALUES ('%d','%ld','%ld','%ld','%ld','%ld','%ld','%ld','%ld','%ld','%ld','%ld','%ld','%ld','%ld','%ld','%ld','%ld','%ld','%ld','%ld','%ld','%ld','%ld','%ld','%ld')",
		guild_castle_db, gc->castle_id, gc->guild_id,  gc->economy, gc->defense, gc->triggerE, gc->triggerD, gc->nextTime, gc->payTime,
		gc->createTime, gc->visibleC, gc->visibleG0, gc->visibleG1, gc->visibleG2, gc->visibleG3, gc->visibleG4, gc->visibleG5,
		gc->visibleG6, gc->visibleG7, gc->Ghp0, gc->Ghp1, gc->Ghp2, gc->Ghp3, gc->Ghp4, gc->Ghp5, gc->Ghp6, gc->Ghp7);


	if(mysql_SendQuery(mysql_handle, tmp_sql) ) {
		ShowMessage("DB server Error - %s\n", mysql_error(mysql_handle) );
		return 0;
	}
	mysql_free_result(sql_res) ; //resource free

	db_foreach(guild_db_, _set_guild_castle, gc->castle_id,gc->guild_id);

	return 0;
}

// Read guild_castle from sql
int inter_guildcastle_fromsql(int castle_id,struct guild_castle *gc)
{
    struct guild_castle *gcopy;
	if (gc==NULL) return 0;
	//ShowMessage("Read from guild_castle\n");

	gcopy = (struct guild_castle*)numdb_search(castle_db_, castle_id);
	if (gcopy == NULL) {
	  gcopy = (struct guild_castle*)aMalloc(sizeof(struct guild_castle));
	  numdb_insert(castle_db_, gc->castle_id, gcopy);
	} else {
	  memcpy(gc, gcopy, sizeof(struct guild_castle));
	  return 0;
	}

	gc->castle_id=castle_id;
	if (castle_id==-1) return 0;
	sprintf(tmp_sql,"SELECT `castle_id`, `guild_id`, `economy`, `defense`, `triggerE`, `triggerD`, `nextTime`, `payTime`, `createTime`, "
		"`visibleC`, `visibleG0`, `visibleG1`, `visibleG2`, `visibleG3`, `visibleG4`, `visibleG5`, `visibleG6`, `visibleG7`,"
		"`Ghp0`, `Ghp1`, `Ghp2`, `Ghp3`, `Ghp4`, `Ghp5`, `Ghp6`, `Ghp7`"
        	" FROM `%s` WHERE `castle_id`='%d'",guild_castle_db, castle_id);
	if(mysql_SendQuery(mysql_handle, tmp_sql) ) {
		ShowMessage("DB server Error - %s\n", mysql_error(mysql_handle) );
		return 0;
	}
	sql_res = mysql_store_result(mysql_handle) ;
	if (sql_res!=NULL && mysql_num_rows(sql_res)>0) {
		sql_row = mysql_fetch_row(sql_res);
		if (sql_row==NULL){
			mysql_free_result(sql_res);
			return 0;
		}

		gc->guild_id =  atoi (sql_row[1]);
		gc->economy = atoi (sql_row[2]);
		gc->defense = atoi (sql_row[3]);
		gc->triggerE = atoi (sql_row[4]);
		gc->triggerD = atoi (sql_row[5]);
		gc->nextTime = atoi (sql_row[6]);
		gc->payTime = atoi (sql_row[7]);
		gc->createTime = atoi (sql_row[8]);
		gc->visibleC = atoi (sql_row[9]);
		gc->visibleG0 = atoi (sql_row[10]);
		gc->visibleG1 = atoi (sql_row[11]);
		gc->visibleG2 = atoi (sql_row[12]);
		gc->visibleG3 = atoi (sql_row[13]);
		gc->visibleG4 = atoi (sql_row[14]);
		gc->visibleG5 = atoi (sql_row[15]);
		gc->visibleG6 = atoi (sql_row[16]);
		gc->visibleG7 = atoi (sql_row[17]);
		gc->Ghp0 = atoi (sql_row[18]);
		gc->Ghp1 = atoi (sql_row[19]);
		gc->Ghp2 = atoi (sql_row[20]);
		gc->Ghp3 = atoi (sql_row[21]);
		gc->Ghp4 = atoi (sql_row[22]);
		gc->Ghp5 = atoi (sql_row[23]);
		gc->Ghp6 = atoi (sql_row[24]);
		gc->Ghp7 = atoi (sql_row[25]);
		//ShowMessage("Read Castle %d of guild %d from sql \n",castle_id,gc->guild_id);
	}
	mysql_free_result(sql_res) ; //resource free
	memcpy(gcopy, gc, sizeof(struct guild_castle));
	return 0;
}

// Read exp_guild.txt
int inter_guild_readdb()
{
	int i;
	FILE *fp;
	char line[1024];
	for (i=0;i<100;i++) guild_exp[i]=0;

	fp=safefopen("db/exp_guild.txt","r");
	if(fp==NULL){
		ShowMessage("can't read %s\n", "db/exp_guild.txt");
		return 1;
	}
	i=0;
	while(fgets(line,256,fp) && i<100){
		if( !skip_empty_line(line) )
			continue;
		guild_exp[i]=atoi(line);
		i++;
	}
	fclose(fp);

	return 0;
}


// Initialize guild sql
int inter_guild_sql_init()
{
	int i;

	guild_db_=numdb_init();
	castle_db_=numdb_init();

	ShowMessage("interserver guild memory initialize.... (%d byte)\n",sizeof(struct guild));

	inter_guild_readdb(); // Read exp

	sprintf (tmp_sql , "SELECT count(*) FROM `%s`",guild_db);
	if(mysql_SendQuery(mysql_handle, tmp_sql) ) {
		ShowMessage("DB server Error - %s\n", mysql_error(mysql_handle) );
		exit(0);
	}
	sql_res = mysql_store_result(mysql_handle) ;
	sql_row = mysql_fetch_row(sql_res);
	ShowMessage("total guild data -> '%s'.......\n",sql_row[0]);
	i = atoi (sql_row[0]);
	mysql_free_result(sql_res);

	if (i > 0) {
		//set party_newid
		sprintf (tmp_sql , "SELECT max(`guild_id`) FROM `%s`",guild_db);
		if(mysql_SendQuery(mysql_handle, tmp_sql) ) {
			ShowMessage("DB server Error - %s\n", mysql_error(mysql_handle) );
			exit(0);
		}

		sql_res = mysql_store_result(mysql_handle) ;
		sql_row = mysql_fetch_row(sql_res);
		guild_newid = atoi(sql_row[0])+1;
		mysql_free_result(sql_res);
	}

	ShowMessage("set guild_newid: %d.......\n",guild_newid);

	return 0;
}

int guild_db_final (void *k, void *data, va_list ap)
{
	struct guild *g = (struct guild *)data;
	if(g)
	{
		if(g->save_timer != -1)
		{	// Save unsaved guild data [Skotlex]
			//delete_timer(g->save_timer,guild_save_timer);
			g->save_timer = -1;
			inter_guild_tosql(g, g->save_flag);

		}
		aFree(g);
	}
	return 0;
}
int castle_db_final (void *k, void *data, va_list ap)
{
	struct guild_castle *gc = (struct guild_castle *)data;
	if (gc) aFree(gc);
	return 0;
}
void inter_guild_sql_final()
{
	numdb_final(guild_db_, guild_db_final);
	numdb_final(castle_db_, castle_db_final);

	return;
}

// Get guild by its name
struct guild* search_guildname(char *str)
{
	char t_name[24];
	int guild_id=0;
	ShowMessage("search_guildname\n");
	sprintf (tmp_sql , "SELECT `guild_id` FROM `%s` WHERE `name`='%s'",guild_db, jstrescapecpy(t_name,str));
	if(mysql_SendQuery(mysql_handle, tmp_sql) ) {
		ShowMessage("DB server Error - %s\n", mysql_error(mysql_handle) );
	}
	sql_res = mysql_store_result(mysql_handle) ;
	if (sql_res!=NULL && mysql_num_rows(sql_res)>0) {
		sql_row = mysql_fetch_row(sql_res);
		guild_id = atoi (sql_row[0]);
	}
	mysql_free_result(sql_res);
	return inter_guild_fromsql(guild_id);
}

// Check if guild is empty
int guild_check_empty(struct guild *g)
{
	int i;
	for(i=0;i<g->max_member;i++){
		if(g->member[i].account_id>0){
			return 0;
		}
	}

	// �N�����Ȃ��̂ŉ��U
	mapif_guild_broken(g->guild_id,0);
	inter_guild_storage_delete(g->guild_id);
	inter_guild_tosql(g,255);
	memset(g,0,sizeof(struct guild));
	return 1;
}

int guild_nextexp(int level)
{
	if(level < 100 && level >0) // Change by hack
		return guild_exp[level-1];

	return 0;
}

// �M���h�X�L�������邩�m�F
int guild_checkskill(struct guild &g, unsigned short id)
{
	unsigned short idx = id - GD_SKILLBASE;
	if(idx >= MAX_GUILDSKILL)
		return 0;
	return g.skill[idx].lv;
}


// �M���h�̏��̍Čv�Z
int guild_calcinfo(struct guild *g)
{
	if(g)
	{
		size_t i,c;
		unsigned long nextexp;
		struct guild before=*g;

		// �X�L��ID�̐ݒ�
		for(i=0;i<MAX_GUILDSKILL;i++)
			g->skill[i].id=i+GD_SKILLBASE;

		// �M���h���x��
		if(g->guild_lv<=0) g->guild_lv=1;
		nextexp = guild_nextexp(g->guild_lv);
		while(g->exp >= nextexp && nextexp > 0)
		{
			g->exp-=nextexp;
			g->guild_lv++;
			g->skill_point++;
			nextexp = guild_nextexp(g->guild_lv);
		}

		// �M���h�̎��̌o���l
		g->next_exp = guild_nextexp(g->guild_lv);

		// �����o����i�M���h�g���K�p�j
		g->max_member = 16 + guild_checkskill(*g, GD_EXTENSION) * 6; //  Guild Extention skill - adds by 6 people per level to Max Member [Lupus]

		// ���σ��x���ƃI�����C���l��
		g->average_lv=0;
		g->connect_member=0;
		for(i=c=0;i<g->max_member;i++){
			if(g->member[i].account_id>0){
				g->average_lv+=g->member[i].lv;
				c++;

				if(g->member[i].online>0)
					g->connect_member++;
			}
		}
		if(c) g->average_lv/=c;

		// �S�f�[�^�𑗂�K�v�����肻��
		if(	g->max_member!=before.max_member	||
			g->guild_lv!=before.guild_lv		||
			g->skill_point!=before.skill_point	){
			mapif_guild_info(-1,g);
			return 1;
		}
	}
	return 0;
}

//-------------------------------------------------------------------
// map server�ւ̒ʐM

// �M���h�쐬��
int mapif_guild_created(int fd,unsigned long account_id,struct guild *g)
{
	if( !session_isActive(fd) )
		return 0;

	WFIFOW(fd,0)=0x3830;
	WFIFOL(fd,2)=account_id;
	if(g!=NULL){
		WFIFOL(fd,6)=g->guild_id;
		ShowMessage("int_guild: created! %d %s\n",g->guild_id,g->name);
	}else{
		WFIFOL(fd,6)=0;
	}
	WFIFOSET(fd,10);
	return 0;
}
// �M���h��񌩂��炸
int mapif_guild_noinfo(int fd,int guild_id)
{
	if( !session_isActive(fd) )
		return 0;

	WFIFOW(fd,0)=0x3831;
	WFIFOW(fd,2)=8;
	WFIFOL(fd,4)=guild_id;
	WFIFOSET(fd,8);
	//ShowError("int_guild: info not found %d\n",guild_id);
	return 0;
}
// �M���h���܂Ƃߑ���
int mapif_guild_info(int fd,struct guild *g)
{
	if(g)
	{
		unsigned char buf[16384];
		WBUFW(buf,0)=0x3831;
		WBUFW(buf,2)=4+sizeof(struct guild);
		guild_tobuffer(*g, buf+4);
		if( !session_isActive(fd) )
			mapif_sendall(buf,4+sizeof(struct guild));
		else
			mapif_send(fd,buf,4+sizeof(struct guild));
		//ShowMessage("int_guild: info %d %s\n", g->guild_id, g->name);
	}
	return 0;
}

// �����o�ǉ���
int mapif_guild_memberadded(int fd,unsigned long guild_id,unsigned long account_id,unsigned long char_id,int flag)
{
	if( !session_isActive(fd) )
		return 0;

	WFIFOW(fd,0)=0x3832;
	WFIFOL(fd,2)=guild_id;
	WFIFOL(fd,6)=account_id;
	WFIFOL(fd,10)=char_id;
	WFIFOB(fd,14)=flag;
	WFIFOSET(fd,15);
	return 0;
}
// �E��/�Ǖ��ʒm
int mapif_guild_leaved(unsigned long guild_id,unsigned long account_id,unsigned long char_id,int flag,
	const char *name,const char *mes)
{
	unsigned char buf[128];
	WBUFW(buf, 0)=0x3834;
	WBUFL(buf, 2)=guild_id;
	WBUFL(buf, 6)=account_id;
	WBUFL(buf,10)=char_id;
	WBUFB(buf,14)=flag;
	memcpy(WBUFP(buf,15),mes,40);
	memcpy(WBUFP(buf,55),name,24);
	mapif_sendall(buf,79);
	ShowMessage("int_guild: guild leaved %d %d %s %s\n",guild_id,account_id,name,mes);
	return 0;
}

// �I�����C����Ԃ�Lv�X�V�ʒm
int mapif_guild_memberinfoshort(struct guild *g,int idx)
{
	unsigned char buf[32];
	WBUFW(buf, 0)=0x3835;
	WBUFL(buf, 2)=g->guild_id;
	WBUFL(buf, 6)=g->member[idx].account_id;
	WBUFL(buf,10)=g->member[idx].char_id;
	WBUFB(buf,14)=(unsigned char)g->member[idx].online;
	WBUFW(buf,15)=g->member[idx].lv;
	WBUFW(buf,17)=g->member[idx].class_;
	mapif_sendall(buf,19);
	return 0;
}

// ���U�ʒm
int mapif_guild_broken(unsigned long guild_id,int flag)
{
	unsigned char buf[16];
	WBUFW(buf,0)=0x3836;
	WBUFL(buf,2)=guild_id;
	WBUFB(buf,6)=flag;
	mapif_sendall(buf,7);
	ShowMessage("int_guild: broken %d\n",guild_id);
	return 0;
}

// �M���h������
int mapif_guild_message(unsigned long guild_id,unsigned long account_id,char *mes,size_t len, int sfd)
{
	unsigned char buf[512];
	WBUFW(buf,0)=0x3837;
	WBUFW(buf,2)=len+12;
	WBUFL(buf,4)=guild_id;
	WBUFL(buf,8)=account_id;
	memcpy(WBUFP(buf,12),mes,len);
	mapif_sendallwos(sfd, buf,len+12);
	return 0;
}

// �M���h��{���ύX�ʒm
int mapif_guild_basicinfochanged(unsigned long guild_id,int type,unsigned long data)
{
	unsigned char buf[2048];
	WBUFW(buf, 0)=0x3839;
	WBUFW(buf, 2)=14;
	WBUFL(buf, 4)=guild_id;
	WBUFW(buf, 8)=type;
	WBUFL(buf,10)=data;
	mapif_sendall(buf,14);
	return 0;
}
// �M���h�����o���ύX�ʒm
int mapif_guild_memberinfochanged(unsigned long guild_id,unsigned long account_id,unsigned long char_id,int type,unsigned long data)
{
	unsigned char buf[2048];
	WBUFW(buf, 0)=0x383a;
	WBUFW(buf, 2)=22;
	WBUFL(buf, 4)=guild_id;
	WBUFL(buf, 8)=account_id;
	WBUFL(buf,12)=char_id;
	WBUFW(buf,16)=type;
	WBUFL(buf,18)=data;
	mapif_sendall(buf,22);
	return 0;
}
// �M���h�X�L���A�b�v�ʒm
int mapif_guild_skillupack(unsigned long guild_id,unsigned long skill_num,unsigned long account_id)
{
	unsigned char buf[16];
	WBUFW(buf, 0)=0x383c;
	WBUFL(buf, 2)=guild_id;
	WBUFL(buf, 6)=skill_num;
	WBUFL(buf,10)=account_id;
	mapif_sendall(buf,14);
	return 0;
}
// �M���h����/�G�Βʒm
int mapif_guild_alliance(unsigned long guild_id1,unsigned long guild_id2,unsigned long account_id1,unsigned long account_id2,
	int flag,const char *name1,const char *name2)
{
	unsigned char buf[128];
	WBUFW(buf, 0)=0x383d;
	WBUFL(buf, 2)=guild_id1;
	WBUFL(buf, 6)=guild_id2;
	WBUFL(buf,10)=account_id1;
	WBUFL(buf,14)=account_id2;
	WBUFB(buf,18)=flag;
	memcpy(WBUFP(buf,19),name1,24);
	memcpy(WBUFP(buf,43),name2,24);
	mapif_sendall(buf,67);
	return 0;
}

// �M���h��E�ύX�ʒm
int mapif_guild_position(struct guild *g, size_t idx)
{
	if(g && idx < MAX_GUILDPOSITION)
	{
		unsigned char buf[128];
		WBUFW(buf,0) = 0x383b;
		WBUFW(buf,2) = sizeof(struct guild_position) + 12;
		WBUFL(buf,4) = g->guild_id;
		WBUFL(buf,8) = idx;
		//memcpy(WBUFP(buf,12), &g->position[idx], sizeof(struct guild_position));
		guild_position_tobuffer(g->position[idx], WBUFP(buf,12));
		mapif_sendall(buf, sizeof(struct guild_position) + 12);
	}
	return 0;
}

// �M���h���m�ύX�ʒm
int mapif_guild_notice(struct guild *g)
{
	unsigned char buf[256];
	WBUFW(buf,0)=0x383e;
	WBUFL(buf,2)=g->guild_id;
	memcpy(WBUFP(buf,6),g->mes1,60);
	memcpy(WBUFP(buf,66),g->mes2,120);
	mapif_sendall(buf,186);
	return 0;
}
// �M���h�G���u�����ύX�ʒm
int mapif_guild_emblem(struct guild *g)
{
	unsigned char buf[2048];
	WBUFW(buf,0)=0x383f;
	WBUFW(buf,2)=g->emblem_len+12;
	WBUFL(buf,4)=g->guild_id;
	WBUFL(buf,8)=g->emblem_id;
	memcpy(WBUFP(buf,12),g->emblem_data,g->emblem_len);
	mapif_sendall(buf,WBUFW(buf,2));
	return 0;
}

int mapif_guild_castle_dataload(int castle_id,int index,int value)      // <Agit>
{
	unsigned char buf[16];
	WBUFW(buf, 0)=0x3840;
	WBUFW(buf, 2)=castle_id;
	WBUFB(buf, 4)=index;
	WBUFL(buf, 5)=value;
	mapif_sendall(buf,9);
	return 0;
}

int mapif_guild_castle_datasave(int castle_id,int index,int value)      // <Agit>
{
	unsigned char buf[16];
	WBUFW(buf, 0)=0x3841;
	WBUFW(buf, 2)=castle_id;
	WBUFB(buf, 4)=index;
	WBUFL(buf, 5)=value;
	mapif_sendall(buf,9);
	return 0;
}

int mapif_guild_castle_alldataload(int fd)
{
	struct guild_castle gc;
	struct guild_castle *gcopy;
	size_t i, len = 4;

	if( !session_isActive(fd) )
		return 0;


	WFIFOW(fd,0) = 0x3842;
	sprintf(tmp_sql, "SELECT * FROM `%s` ORDER BY `castle_id`", guild_castle_db);
	if (mysql_SendQuery(mysql_handle, tmp_sql)) {
		ShowMessage("DB server Error - %s\n", mysql_error(mysql_handle) );
	}
	sql_res = mysql_store_result(mysql_handle);
	if (sql_res != NULL && mysql_num_rows(sql_res) > 0)
	{
		for(i = 0; ((sql_row = mysql_fetch_row(sql_res)) && i < MAX_GUILDCASTLE); i++)
		{
			memset(&gc, 0, sizeof(struct guild_castle));
			gc.castle_id = atoi(sql_row[0]);
			gc.guild_id =  atoi(sql_row[1]);
			gc.economy = atoi(sql_row[2]);
			gc.defense = atoi(sql_row[3]);
			gc.triggerE = atoi(sql_row[4]);
			gc.triggerD = atoi(sql_row[5]);
			gc.nextTime = atoi(sql_row[6]);
			gc.payTime = atoi(sql_row[7]);
			gc.createTime = atoi(sql_row[8]);
			gc.visibleC = atoi(sql_row[9]);
			gc.visibleG0 = atoi(sql_row[10]);
			gc.visibleG1 = atoi(sql_row[11]);
			gc.visibleG2 = atoi(sql_row[12]);
			gc.visibleG3 = atoi(sql_row[13]);
			gc.visibleG4 = atoi(sql_row[14]);
			gc.visibleG5 = atoi(sql_row[15]);
			gc.visibleG6 = atoi(sql_row[16]);
			gc.visibleG7 = atoi(sql_row[17]);
			gc.Ghp0 = atoi(sql_row[18]);
			gc.Ghp1 = atoi(sql_row[19]);
			gc.Ghp2 = atoi(sql_row[20]);
			gc.Ghp3 = atoi(sql_row[21]);
			gc.Ghp4 = atoi(sql_row[22]);
			gc.Ghp5 = atoi(sql_row[23]);
			gc.Ghp6 = atoi(sql_row[24]);
			gc.Ghp7 = atoi(sql_row[25]);

			//memcpy(WFIFOP(fd,len), gc, sizeof(struct guild_castle));
			guild_castle_tobuffer(gc,WFIFOP(fd,len));

			gcopy = (struct guild_castle*)numdb_search(castle_db_,gc.castle_id);
			if (gcopy == NULL)
			{
				gcopy = (struct guild_castle *) aMalloc(sizeof(struct guild_castle));
				numdb_insert(castle_db_, gc.castle_id, gcopy);
			}
			memcpy(gcopy, &gc, sizeof(struct guild_castle));
			len += sizeof(struct guild_castle);
		}
	}
	mysql_free_result(sql_res);
	WFIFOW(fd,2) = len;
	WFIFOSET(fd,len);
	return 0;
}


//-------------------------------------------------------------------
// map server����̒ʐM


// �M���h�쐬�v��
int mapif_parse_CreateGuild(int fd,unsigned long account_id,char *name,unsigned char *buf)
{
	struct guild *g;
	size_t i;

	ShowMessage("CreateGuild\n");
	g=search_guildname(name);
	if(g!=NULL&&g->guild_id>0){
		ShowMessage("int_guild: same name guild exists [%s]\n",name);
		mapif_guild_created(fd,account_id,NULL);
		return 0;
	}

	memset(g,0,sizeof(struct guild));
	g->guild_id=guild_newid++;
	memcpy(g->name,name,24);
	guild_member_frombuffer(g->member[0], buf);
	memcpy(g->master, g->member[0].name, 24);


	g->position[0].mode=0x11;
	strcpy(g->position[0].name,"GuildMaster");
	strcpy(g->position[MAX_GUILDPOSITION-1].name,"Newbie");
	for(i=1; i<MAX_GUILDPOSITION-1; i++)
		sprintf(g->position[i].name,"Position %d",i+1);

	// Initialize guild property
	g->max_member=16;
	g->average_lv=g->member[0].lv;
	g->castle_id=0xFFFF;
	for(i=0;i<MAX_GUILDSKILL;i++)
		g->skill[i].id = i + GD_SKILLBASE;

	// Save to sql
	ShowMessage("Create initialize OK!\n");
	i=inter_guild_tosql(g,255);

	if (i<0) {
		mapif_guild_created(fd,account_id,NULL);
		return 0;
	}

	// Report to client
	mapif_guild_created(fd,account_id,g);
	mapif_guild_info(fd,g);

	if(log_inter)
		inter_log("guild %s (id=%d) created by master %s (id=%d)" RETCODE,
			name, g->guild_id, g->member[0].name, g->member[0].account_id );


	return 0;
}
// Return guild info to client
int mapif_parse_GuildInfo(int fd,int guild_id)
{
	struct guild * g = inter_guild_fromsql(guild_id);
	if(g!=NULL && g->guild_id!=0xFFFFFFFF){
		guild_calcinfo(g);
		mapif_guild_info(fd,g);
		//inter_guild_tosql(g,1); // Change guild
	}else
		mapif_guild_noinfo(fd,guild_id);
	return 0;
}
// Add member to guild
int mapif_parse_GuildAddMember(int fd,int guild_id,unsigned char *buf)
{
	struct guild *g = inter_guild_fromsql(guild_id);
	struct guild_member member;
	int i;

	guild_member_frombuffer(member,buf);
	if(g==NULL||g->guild_id<=0){
		mapif_guild_memberadded(fd,guild_id,member.account_id,member.char_id,1);
		return 0;
	}

	for(i=0;i<g->max_member;i++){
		if(g->member[i].account_id==0){

			memcpy(&g->member[i], &member,sizeof(struct guild_member));
			mapif_guild_memberadded(fd,guild_id,member.account_id,member.char_id,0);
			guild_calcinfo(g);
			mapif_guild_info(-1,g);
			inter_guild_tosql(g,3); // Change guild & guild_member
			return 0;
		}
	}
	mapif_guild_memberadded(fd,guild_id,member.account_id,member.char_id,1);
	//inter_guild_tosql(g,3); // Change guild & guild_member
	return 0;
}
// Delete member from guild
int mapif_parse_GuildLeave(int fd,unsigned long guild_id,unsigned long account_id,unsigned long char_id,int flag,const char *mes)
{
	struct guild *g= inter_guild_fromsql(guild_id);

	if(g!=NULL&&g->guild_id>0){
		int i;
		for(i=0;i<g->max_member;i++){
			if( g->member[i].account_id==account_id &&
				g->member[i].char_id==char_id)
			{
				ShowMessage("%d %d\n",i, (int)(&g->member[i]));
				ShowMessage("%d %s\n",i, g->member[i].name);

				if(flag)
				{	// �Ǖ��̏ꍇ�Ǖ����X�g�ɓ����
					size_t j;
					for(j=0;j<MAX_GUILDEXPLUSION;j++)
					{
						if(g->explusion[j].account_id==0)
							break;
					}
					if(j==MAX_GUILDEXPLUSION)
					{	// ��t�Ȃ̂ŌÂ��̂�����
						for(j=0;j<MAX_GUILDEXPLUSION-1;j++)
							g->explusion[j]=g->explusion[j+1];
						j=MAX_GUILDEXPLUSION-1;
					}
					g->explusion[j].account_id = account_id;
					g->explusion[j].char_id    = char_id;
					memcpy(g->explusion[j].acc,"dummy",24);
					memcpy(g->explusion[j].name,g->member[i].name,24);
					memcpy(g->explusion[j].mes,mes,40);
					g->explusion[j].mes[39]=0;
				}

				mapif_guild_leaved(guild_id,account_id,char_id,flag,g->member[i].name,mes);
				ShowMessage("%d %s\n",i, g->member[i].name);
				memset(&g->member[i],0,sizeof(struct guild_member));

				if( guild_check_empty(g)==0 )
					mapif_guild_info(-1,g);// �܂��l������̂Ńf�[�^���M
				/*
				else
					inter_guild_save();	// ���U�����̂ň�cZ�[�u
				return 0;*/
			}
		}
		guild_calcinfo(g);
		inter_guild_tosql(g,19); // Change guild & guild_member & guild_expulsion
	}else{
		sprintf(tmp_sql, "UPDATE `%s` SET `guild_id`='0' WHERE `account_id`='%ld' AND `char_id`='%ld'",char_db, account_id,char_id);
		if(mysql_SendQuery(mysql_handle, tmp_sql) ) {
			ShowMessage("DB server Error (update `char`)- %s\n", mysql_error(mysql_handle) );
		}
		/* mapif_guild_leaved(guild_id,account_id,char_id,flag,g->member[i].name,mes);	*/
	}

	return 0;
}
// Change member info
int mapif_parse_GuildChangeMemberInfoShort(int fd,unsigned long guild_id,
	unsigned long account_id,unsigned long char_id,int online,int lv,int class_)
{
	// Could speed up by manipulating only guild_member
	struct guild * g= inter_guild_fromsql(guild_id);
	int i,alv,c, idx;

	if(g==NULL||g->guild_id<=0)
		return 0;

	g->connect_member=0;

	idx = -1;

	for(i=0,alv=0,c=0;i<g->max_member;i++){
		if(	g->member[i].account_id==account_id &&
			g->member[i].char_id==char_id){

			g->member[i].online=online;
			g->member[i].lv=lv;
			g->member[i].class_=class_;
			mapif_guild_memberinfoshort(g,i);
			idx = i;
		}
		if( g->member[i].account_id>0 ){
			alv+=g->member[i].lv;
			c++;
		}
		if( g->member[i].online )
			g->connect_member++;
	}

	if (c)
		// ���σ��x��
		g->average_lv=alv/c;

	sprintf(tmp_sql, "UPDATE `%s` SET `connect_member`=%d,`average_lv`=%d WHERE `guild_id`='%ld'", guild_db,  g->connect_member, g->average_lv,  g->guild_id);
	if(mysql_SendQuery(mysql_handle, tmp_sql) )
	  ShowMessage("DB server Error: %s - %s\n", tmp_sql, mysql_error(mysql_handle) );

	sprintf(tmp_sql, "UPDATE `%s` SET `online`=%d,`lv`=%d,`class`=%d WHERE `char_id`=%ld", guild_member_db, g->member[idx].online, g->member[idx].lv, g->member[idx].class_, g->member[idx].char_id);
	if(mysql_SendQuery(mysql_handle, tmp_sql) )
	  ShowMessage("DB server Error: %s - %s\n", tmp_sql, mysql_error(mysql_handle) );

	return 0;
}

// BreakGuild
int mapif_parse_BreakGuild(int fd,int guild_id)
{
	struct guild *g= inter_guild_fromsql(guild_id);
	if(g==NULL)
		return 0;

	// Delete guild from sql
	//ShowMessage("- Delete guild %d from guild\n",guild_id);
	sprintf(tmp_sql, "DELETE FROM `%s` WHERE `guild_id`='%d'",guild_db, guild_id);
	if(mysql_SendQuery(mysql_handle, tmp_sql) ) {
		ShowMessage("DB server Error (delete `guild`)- %s\n", mysql_error(mysql_handle) );
	}
	//ShowMessage("- Delete guild %d from guild_member\n",guild_id);
	sprintf(tmp_sql, "DELETE FROM `%s` WHERE `guild_id`='%d'",guild_member_db, guild_id);
	if(mysql_SendQuery(mysql_handle, tmp_sql) ) {
		ShowMessage("DB server Error (delete `guild_member`)- %s\n", mysql_error(mysql_handle) );
	}
	//ShowMessage("- Delete guild %d from guild_skill\n",guild_id);
	sprintf(tmp_sql, "DELETE FROM `%s` WHERE `guild_id`='%d'",guild_skill_db, guild_id);
	if(mysql_SendQuery(mysql_handle, tmp_sql) ) {
		ShowMessage("DB server Error (delete `guild_skill`)- %s\n", mysql_error(mysql_handle) );
	}
	//ShowMessage("- Delete guild %d from guild_position\n",guild_id);
	sprintf(tmp_sql, "DELETE FROM `%s` WHERE `guild_id`='%d'",guild_position_db, guild_id);
	if(mysql_SendQuery(mysql_handle, tmp_sql) ) {
		ShowMessage("DB server Error (delete `guild_position`)- %s\n", mysql_error(mysql_handle) );
	}
	//ShowMessage("- Delete guild %d from guild_expulsion\n",guild_id);
	sprintf(tmp_sql, "DELETE FROM `%s` WHERE `guild_id`='%d'",guild_expulsion_db, guild_id);
	if(mysql_SendQuery(mysql_handle, tmp_sql) ) {
		ShowMessage("DB server Error (delete `guild_expulsion`)- %s\n", mysql_error(mysql_handle) );
	}
	//ShowMessage("- Delete guild %d from guild_alliance\n",guild_id);
	sprintf(tmp_sql, "DELETE FROM `%s` WHERE `guild_id`='%d' OR `alliance_id`='%d'",guild_alliance_db, guild_id,guild_id);
	if(mysql_SendQuery(mysql_handle, tmp_sql) ) {
		ShowMessage("DB server Error (delete `guild_position`)- %s\n", mysql_error(mysql_handle) );
	}

	//ShowMessage("- Delete guild %d from guild_castle\n",guild_id);
	sprintf(tmp_sql, "DELETE FROM `%s` WHERE `guild_id`='%d'",guild_castle_db, guild_id);
	if(mysql_SendQuery(mysql_handle, tmp_sql) ) {
		ShowMessage("DB server Error (delete `guild_position`)- %s\n", mysql_error(mysql_handle) );
	}

	db_foreach(castle_db_, _erase_guild, guild_id);

	//ShowMessage("- Update guild %d of char\n",guild_id);
	sprintf(tmp_sql, "UPDATE `%s` SET `guild_id`='0' WHERE `guild_id`='%d'",char_db, guild_id);
	if(mysql_SendQuery(mysql_handle, tmp_sql) ) {
		ShowMessage("DB server Error (delete `guild_position`)- %s\n", mysql_error(mysql_handle) );
	}

	inter_guild_storage_delete(guild_id);
	mapif_guild_broken(guild_id,0);

	if(log_inter)
		inter_log("guild %s (id=%d) broken" RETCODE,g->name,guild_id);

	return 0;
}

// �M���h���b�Z�[�W���M
int mapif_parse_GuildMessage(int fd,unsigned long guild_id,unsigned long account_id,char *mes,size_t len)
{
	return mapif_guild_message(guild_id,account_id,mes,len, fd);
}
// �M���h��{�f�[�^�ύX�v��
int mapif_parse_GuildBasicInfoChange(int fd,unsigned long guild_id,int type, unsigned long data)
{
	struct guild * g = inter_guild_fromsql(guild_id);

	if(g==NULL||g->guild_id<=0)
		return 0;
	switch(type){
	case GBI_GUILDLV: {
		ShowMessage("GBI_GUILDLV\n");
			if(g->guild_lv+data<=50){
				g->guild_lv+=data;
				g->skill_point+=data;
			}else if(g->guild_lv+data>=1)
				g->guild_lv+=data;
			mapif_guild_info(-1,g);
			inter_guild_tosql(g,1);
		} return 0;
	default:
		ShowMessage("int_guild: GuildBasicInfoChange: Unknown type %d\n",type);
		break;
	}
	mapif_guild_basicinfochanged(guild_id,type,data);
	inter_guild_tosql(g,1); // Change guild
	return 0;
}

// �M���h�����o�f�[�^�ύX�v��
int mapif_parse_GuildMemberInfoChange(int fd,unsigned long guild_id,unsigned long account_id,unsigned long char_id, unsigned short type, unsigned long data)
{
	// Could make some improvement in speed, because only change guild_member
	int i;
	struct guild* g = inter_guild_fromsql(guild_id);

	ShowMessage("GuildMemberInfoChange %s \n",(type==GMI_EXP)?"GMI_EXP":"OTHER");

	if(g==NULL){
		return 0;
	}
	for(i=0;i<g->max_member;i++)
		if(	g->member[i].account_id==account_id &&
			g->member[i].char_id==char_id )
				break;
	if(i==g->max_member){
		ShowError("int_guild: GuildMemberChange: Not found %d,%d in %d[%s]\n",
			account_id,char_id,guild_id,g->name);
		return 0;
	}
	switch(type){
	case GMI_POSITION:	// ��E
	{
	    g->member[i].position=data;
	    mapif_guild_memberinfochanged(guild_id,account_id,char_id,type,data);
	    inter_guild_tosql(g,3); // Change guild & guild_member
	    break;
	}
	case GMI_EXP:
	{	// EXP
	    int exp,oldexp=g->member[i].exp;
	    exp=g->member[i].exp=data;
	    g->exp+=(exp-oldexp);
	    guild_calcinfo(g);	// Lv�A�b�v���f
	    mapif_guild_basicinfochanged(guild_id,GBI_EXP,g->exp);
	    mapif_guild_memberinfochanged(guild_id,account_id,char_id,type,data);

	    sprintf(tmp_sql, "UPDATE `%s` SET `guild_lv`=%d,`connect_member`=%d,`max_member`=%d,`average_lv`=%d,`exp`=%ld,`next_exp`=%ld,`skill_point`=%d WHERE `guild_id`='%ld'", guild_db, g->guild_lv, g->connect_member, g->max_member, g->average_lv, g->exp, g->next_exp, g->skill_point, g->guild_id);
	    if(mysql_SendQuery(mysql_handle, tmp_sql) )
	      ShowMessage("DB server Error: %s - %s\n", tmp_sql, mysql_error(mysql_handle) );

	    sprintf(tmp_sql, "UPDATE `%s` SET `exp`=%ld  WHERE `char_id`=%ld", guild_member_db, g->member[i].exp, g->member[i].char_id);
	    if(mysql_SendQuery(mysql_handle, tmp_sql) )
	      ShowMessage("DB server Error: %s - %s\n", tmp_sql, mysql_error(mysql_handle) );
	    break;
	}
	default:
	  ShowMessage("int_guild: GuildMemberInfoChange: Unknown type %d\n",type);
	  break;
	}
	return 0;
}

// �M���h��E���ύX�v��
int mapif_parse_GuildPosition(int fd, unsigned long guild_id, unsigned long idx, unsigned char *buf)
{
	// Could make some improvement in speed, because only change guild_position
	struct guild * g = inter_guild_fromsql(guild_id);

	if(g && idx<MAX_GUILDPOSITION)
	{
		guild_position_frombuffer(g->position[idx],buf);
		mapif_guild_position(g,idx);
		ShowMessage("int_guild: position changed %d\n",idx);
		inter_guild_tosql(g,4); // Change guild_position
	}
	return 0;
}
// �M���h�X�L���A�b�v�v��
int mapif_parse_GuildSkillUp(int fd,unsigned long guild_id,int skill_num,int account_id)
{
	// Could make some improvement in speed, because only change guild_position
	struct guild *g = inter_guild_fromsql(guild_id);
	int idx = skill_num - GD_SKILLBASE;


	if(g == NULL || idx < 0 || idx >= MAX_GUILDSKILL)
		return 0;
	//ShowMessage("GuildSkillUp\n");

	if(	g->skill_point>0 && g->skill[idx].id>0 &&
		g->skill[idx].lv<10 ){
		g->skill[idx].lv++;
		g->skill_point--;
		if(guild_calcinfo(g)==0)
			mapif_guild_info(-1,g);
		mapif_guild_skillupack(guild_id,skill_num,account_id);
		ShowMessage("int_guild: skill %d up\n",skill_num);
		inter_guild_tosql(g,33); // Change guild & guild_skill
	}
	return 0;
}
// �M���h�����v��
int mapif_parse_GuildAlliance(int fd,unsigned long guild_id1,unsigned long guild_id2,
	unsigned long account_id1,unsigned long account_id2,int flag)
{
	// Could speed up
	struct guild *g[2];
	int j,i;
	g[0]= inter_guild_fromsql(guild_id1);
	g[1]= inter_guild_fromsql(guild_id2);

	if(g[0]==NULL || g[1]==NULL || g[0]->guild_id ==0 || g[1]->guild_id==0)
		return 0;

	if(!(flag&0x8)){
		for(i=0;i<2-(flag&1);i++){
			for(j=0;j<MAX_GUILDALLIANCE;j++)
				if(g[i]->alliance[j].guild_id==0){
					g[i]->alliance[j].guild_id=g[1-i]->guild_id;
					memcpy(g[i]->alliance[j].name,g[1-i]->name,24);
					g[i]->alliance[j].opposition=flag&1;
					break;
				}
		}
	}else{	// �֌W����
		for(i=0;i<2-(flag&1);i++){
			for(j=0;j<MAX_GUILDALLIANCE;j++)
				if(	g[i]->alliance[j].guild_id==g[1-i]->guild_id &&
					g[i]->alliance[j].opposition==(flag&1)){
					g[i]->alliance[j].guild_id=0;
					break;
				}
		}
	}
	mapif_guild_alliance(guild_id1,guild_id2,account_id1,account_id2,flag,
		g[0]->name,g[1]->name);
	inter_guild_tosql(g[0],8); // Change guild_alliance
	inter_guild_tosql(g[1],8); // Change guild_alliance
	return 0;
}
// �M���h���m�ύX�v��
int mapif_parse_GuildNotice(int fd,int guild_id,const char *mes1,const char *mes2)
{
	struct guild *g= inter_guild_fromsql(guild_id);

	if(g==NULL||g->guild_id<=0)
		return 0;
	memcpy(g->mes1,mes1,60);
	memcpy(g->mes2,mes2,120);
	inter_guild_tosql(g,1); // Change mes of guild
	return mapif_guild_notice(g);
}
// �M���h�G���u�����ύX�v��
int mapif_parse_GuildEmblem(int fd,int len,int guild_id,int dummy,const char *data)
{
	struct guild * g= inter_guild_fromsql(guild_id);

	if(g==NULL||g->guild_id<=0)
		return 0;
	memcpy(g->emblem_data,data,len);
	g->emblem_len=len;
	g->emblem_id++;
	inter_guild_tosql(g,1); // Change guild
	return mapif_guild_emblem(g);
}

int mapif_parse_GuildCastleDataLoad(int fd,int castle_id,int index)     // <Agit>
{
	struct guild_castle gc;

	inter_guildcastle_fromsql(castle_id, &gc);

	if(gc.castle_id ==0xFFFF )
		return mapif_guild_castle_dataload(castle_id,0,0);

	switch(index){
	case 1: return mapif_guild_castle_dataload(gc.castle_id,index,gc.guild_id); break;
	case 2: return mapif_guild_castle_dataload(gc.castle_id,index,gc.economy); break;
	case 3: return mapif_guild_castle_dataload(gc.castle_id,index,gc.defense); break;
	case 4: return mapif_guild_castle_dataload(gc.castle_id,index,gc.triggerE); break;
	case 5: return mapif_guild_castle_dataload(gc.castle_id,index,gc.triggerD); break;
	case 6: return mapif_guild_castle_dataload(gc.castle_id,index,gc.nextTime); break;
	case 7: return mapif_guild_castle_dataload(gc.castle_id,index,gc.payTime); break;
	case 8: return mapif_guild_castle_dataload(gc.castle_id,index,gc.createTime); break;
	case 9: return mapif_guild_castle_dataload(gc.castle_id,index,gc.visibleC); break;
	case 10: return mapif_guild_castle_dataload(gc.castle_id,index,gc.visibleG0); break;
	case 11: return mapif_guild_castle_dataload(gc.castle_id,index,gc.visibleG1); break;
	case 12: return mapif_guild_castle_dataload(gc.castle_id,index,gc.visibleG2); break;
	case 13: return mapif_guild_castle_dataload(gc.castle_id,index,gc.visibleG3); break;
	case 14: return mapif_guild_castle_dataload(gc.castle_id,index,gc.visibleG4); break;
	case 15: return mapif_guild_castle_dataload(gc.castle_id,index,gc.visibleG5); break;
	case 16: return mapif_guild_castle_dataload(gc.castle_id,index,gc.visibleG6); break;
	case 17: return mapif_guild_castle_dataload(gc.castle_id,index,gc.visibleG7); break;
	case 18: return mapif_guild_castle_dataload(gc.castle_id,index,gc.Ghp0); break;	// guardian HP [Valaris]
	case 19: return mapif_guild_castle_dataload(gc.castle_id,index,gc.Ghp1); break;
	case 20: return mapif_guild_castle_dataload(gc.castle_id,index,gc.Ghp2); break;
	case 21: return mapif_guild_castle_dataload(gc.castle_id,index,gc.Ghp3); break;
	case 22: return mapif_guild_castle_dataload(gc.castle_id,index,gc.Ghp4); break;
	case 23: return mapif_guild_castle_dataload(gc.castle_id,index,gc.Ghp5); break;
	case 24: return mapif_guild_castle_dataload(gc.castle_id,index,gc.Ghp6); break;
	case 25: return mapif_guild_castle_dataload(gc.castle_id,index,gc.Ghp7); break;	// end additions [Valaris]
	default:
		ShowError("mapif_parse_GuildCastleDataLoad ERROR!! (Not found index=%d)\n", index);
		return 0;
	}
}

int mapif_parse_GuildCastleDataSave(int fd,int castle_id,int index,int value)   // <Agit>
{
	struct guild_castle gc;
	inter_guildcastle_fromsql(castle_id, &gc);

	if(gc.castle_id == 0xFFFF)
		return mapif_guild_castle_datasave(castle_id,index,value);

	switch(index){
	case 1:
		if( gc.guild_id!=(unsigned long)value ){
			int gid=(value)?value:gc.guild_id;
			struct guild *g=inter_guild_fromsql(gid);
			if(log_inter)
				inter_log("guild %s (id=%d) %s castle id=%d" RETCODE,
					(g)?g->name:"??" ,gid, (value)?"occupy":"abandon", castle_id);
		}
		gc.guild_id = value;
		break;
	case 2: gc.economy = value; break;
	case 3: gc.defense = value; break;
	case 4: gc.triggerE = value; break;
	case 5: gc.triggerD = value; break;
	case 6: gc.nextTime = value; break;
	case 7: gc.payTime = value; break;
	case 8: gc.createTime = value; break;
	case 9: gc.visibleC = value; break;
	case 10: gc.visibleG0 = value; break;
	case 11: gc.visibleG1 = value; break;
	case 12: gc.visibleG2 = value; break;
	case 13: gc.visibleG3 = value; break;
	case 14: gc.visibleG4 = value; break;
	case 15: gc.visibleG5 = value; break;
	case 16: gc.visibleG6 = value; break;
	case 17: gc.visibleG7 = value; break;
	case 18: gc.Ghp0 = value; break;	// guardian HP [Valaris]
	case 19: gc.Ghp1 = value; break;
	case 20: gc.Ghp2 = value; break;
	case 21: gc.Ghp3 = value; break;
	case 22: gc.Ghp4 = value; break;
	case 23: gc.Ghp5 = value; break;
	case 24: gc.Ghp6 = value; break;
	case 25: gc.Ghp7 = value; break;	// end additions [Valaris]
	default:
		ShowError("mapif_parse_GuildCastleDataSave ERROR!! (Not found index=%d)\n", index);
		return 0;
	}
	inter_guildcastle_tosql(&gc);
	return mapif_guild_castle_datasave(gc.castle_id,index,value);
}

// �M���h�`�F�b�N�v�� == Guild Check Request
int mapif_parse_GuildCheck(int fd,unsigned long guild_id,unsigned long account_id,unsigned long char_id)
{
	// What does this mean? Check if belong to another guild?
	return 0;
}

// map server ����̒ʐM
// �E�P�p�P�b�g�̂݉�͂��邱��
// �E�p�P�b�g���f�[�^��inter.c�ɃZ�b�g���Ă�������
// �E�p�P�b�g���`�F�b�N��ARFIFOSKIP�͌Ăяo�����ōs����̂ōs���Ă͂Ȃ�Ȃ�
// �E�G���[�Ȃ�0(false)�A�����łȂ��Ȃ�1(true)���������Ȃ���΂Ȃ�Ȃ�
int inter_guild_parse_frommap(int fd)
{
	if( !session_isActive(fd) )
		return 0;

	switch(RFIFOW(fd,0)){
	case 0x3030: mapif_parse_CreateGuild(fd,RFIFOL(fd,4),(char*)RFIFOP(fd,8),RFIFOP(fd,32)); break;
	case 0x3031: mapif_parse_GuildInfo(fd,RFIFOL(fd,2)); break;
	case 0x3032: mapif_parse_GuildAddMember(fd,RFIFOL(fd,4),RFIFOP(fd,8)); break;
	case 0x3034: mapif_parse_GuildLeave(fd,RFIFOL(fd,2),RFIFOL(fd,6),RFIFOL(fd,10),RFIFOB(fd,14),(const char*)RFIFOP(fd,15)); break;
	case 0x3035: mapif_parse_GuildChangeMemberInfoShort(fd,RFIFOL(fd,2),RFIFOL(fd,6),RFIFOL(fd,10),RFIFOB(fd,14),RFIFOW(fd,15),RFIFOW(fd,17)); break;
	case 0x3036: mapif_parse_BreakGuild(fd,RFIFOL(fd,2)); break;
	case 0x3037: mapif_parse_GuildMessage(fd,RFIFOL(fd,4),RFIFOL(fd,8),(char*)RFIFOP(fd,12),RFIFOW(fd,2)-12); break;
	case 0x3038: mapif_parse_GuildCheck(fd,RFIFOL(fd,2),RFIFOL(fd,6),RFIFOL(fd,10)); break;
	case 0x3039: mapif_parse_GuildBasicInfoChange(fd,RFIFOL(fd,4),RFIFOW(fd,8), RFIFOL(fd,10)); break;
	case 0x303A: mapif_parse_GuildMemberInfoChange(fd,RFIFOL(fd,4),RFIFOL(fd,8),RFIFOL(fd,12),RFIFOW(fd,16),RFIFOL(fd,18)); break;
	case 0x303B: mapif_parse_GuildPosition(fd,RFIFOL(fd,4),RFIFOL(fd,8),RFIFOP(fd,12)); break;
	case 0x303C: mapif_parse_GuildSkillUp(fd,RFIFOL(fd,2),RFIFOL(fd,6),RFIFOL(fd,10)); break;
	case 0x303D: mapif_parse_GuildAlliance(fd,RFIFOL(fd,2),RFIFOL(fd,6),RFIFOL(fd,10),RFIFOL(fd,14),RFIFOB(fd,18)); break;
	case 0x303E: mapif_parse_GuildNotice(fd,RFIFOL(fd,2),(const char*)RFIFOP(fd,6),(const char*)RFIFOP(fd,66)); break;
	case 0x303F: mapif_parse_GuildEmblem(fd,RFIFOW(fd,2)-12,RFIFOL(fd,4),RFIFOL(fd,8),(const char*)RFIFOP(fd,12)); break;
	case 0x3040: mapif_parse_GuildCastleDataLoad(fd,RFIFOW(fd,2),RFIFOB(fd,4)); break;
	case 0x3041: mapif_parse_GuildCastleDataSave(fd,RFIFOW(fd,2),RFIFOB(fd,4),RFIFOL(fd,5)); break;

	default:
		return 0;
	}
	return 1;
}

int inter_guild_mapif_init(int fd)
{
	return mapif_guild_castle_alldataload(fd);
}

// �T�[�o�[����E�ޗv���i�L�����폜�p�j
int inter_guild_leave(unsigned long guild_id,unsigned long account_id,unsigned long char_id)
{
	return mapif_parse_GuildLeave(-1,guild_id,account_id,char_id,0,"**�T�[�o�[����**");
}