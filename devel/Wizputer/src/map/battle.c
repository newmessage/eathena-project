// $Id: battle.c,v 1.10 2004/09/29 21:08:17 Akitasha Exp $
#include "base.h"
#include "nullpo.h"
#include "showmsg.h"
#include "utils.h"


#include "battle.h"
#include "map.h"
#include "pc.h"
#include "status.h"
#include "skill.h"
#include "mob.h"
#include "itemdb.h"
#include "clif.h"
#include "pet.h"
#include "guild.h"

#define	is_boss(bl)	status_get_mexp(bl)	// Can refine later [Aru]

int attr_fix_table[4][10][10];

struct Battle_Config battle_config;



/*==========================================
 * ���������b�N���Ă���MOB��?��?����(foreachclient)
 *------------------------------------------
 */
int battle_counttargeted_sub(struct block_list &bl, va_list ap)
{
	unsigned long id;
	int *c;
	unsigned short target_lv;
	struct block_list *src;

	nullpo_retr(0, ap);
	id = va_arg(ap,int);
	nullpo_retr(0, c = va_arg(ap, int *));
	src = va_arg(ap,struct block_list *);
	target_lv = (unsigned short)va_arg(ap,int);

	if (id == bl.id || (src && id == src->id))
		return 0;
	if (bl.type == BL_PC)
	{
		struct map_session_data &sd = (struct map_session_data &)bl;
		if(sd.attacktarget == id && sd.attacktimer != -1 && sd.attacktarget_lv >= target_lv)
			(*c)++;
	}
	else if (bl.type == BL_MOB)
	{
		struct mob_data &md = (struct mob_data &)bl;
		if(md.target_id == id && md.timer != -1 && md.state.state == MS_ATTACK && md.target_lv >= target_lv)		
			(*c)++;
		//ShowMessage("md->target_lv:%d, target_lv:%d\n", md->target_lv, target_lv);
	}
	else if (bl.type == BL_PET)
	{
		struct pet_data &pd = (struct pet_data &)bl;
		if( pd.target_id == id && pd.timer != -1 && pd.state.state == MS_ATTACK && pd.target_lv >= target_lv)
			(*c)++;
	}

	return 0;
}
/*==========================================
 * ���������b�N���Ă���Ώۂ̐���Ԃ�(�ėp)
 * �߂�͐�����0�ȏ�
 *------------------------------------------
 */
unsigned int battle_counttargeted(struct block_list &bl,struct block_list *src,  unsigned short target_lv)
{
	unsigned int c = 0;

	map_foreachinarea(battle_counttargeted_sub, bl.m,
		((int)bl.x)-AREA_SIZE, ((int)bl.y)-AREA_SIZE,
		((int)bl.x)+AREA_SIZE, ((int)bl.y)+AREA_SIZE, 0,
		bl.id, &c, src, target_lv);

	return c;
}

/*==========================================
 * Get random targetting enemy
 *------------------------------------------
 */
int battle_gettargeted_sub(struct block_list &bl, va_list ap)
{
	struct block_list **bl_list;
	struct block_list *target;
	int *c;

	nullpo_retr(0, ap);

	bl_list = va_arg(ap, struct block_list **);
	nullpo_retr(0, c = va_arg(ap, int *));
	nullpo_retr(0, target = va_arg(ap, struct block_list *));

	if (bl.id == target->id)
		return 0;
	if (*c >= 24)
		return 0;

	if (bl.type == BL_PC)
	{
		struct map_session_data &sd = (struct map_session_data &)bl;
		if(sd.attacktarget != target->id || sd.attacktimer == -1)
			return 0;
	}
	else if (bl.type == BL_MOB)
	{
		struct mob_data &md = (struct mob_data &)bl;
		if(md.target_id != target->id || md.timer == -1 || md.state.state != MS_ATTACK)
			return 0;
	}
	else if (bl.type == BL_PET)
	{
		struct pet_data &pd = (struct pet_data &)bl;
		if(pd.target_id != target->id || pd.timer == -1 || pd.state.state != MS_ATTACK)
			return 0;
	}
	bl_list[(*c)++] = &bl;
	return 0;
}
struct block_list* battle_gettargeted(struct block_list &target)
{
	struct block_list *bl_list[24];
	int c = 0;

	memset(bl_list, 0, sizeof(bl_list));
	map_foreachinarea(battle_gettargeted_sub, target.m,
		((int)target.x)-AREA_SIZE, ((int)target.y)-AREA_SIZE,
		((int)target.x)+AREA_SIZE, ((int)target.y)+AREA_SIZE, 0, bl_list, &c, &target);
	if (c == 0 || c > 24)
		return NULL;
	return bl_list[rand()%c];
}

// �_���[�W�̒x��
struct delay_damage {
	struct block_list *src;
	int target;
	int damage;
	int flag;
};

int battle_delay_damage_sub(int tid,unsigned long tick,int id,int data)
{
	struct delay_damage *dat = (struct delay_damage *)data;
	if(dat)
	{
	struct block_list *target = map_id2bl(dat->target);
		if (target && map_id2bl(id) == dat->src && target->prev != NULL)
		battle_damage(dat->src, target, dat->damage, dat->flag);

	aFree(dat);
	}
	return 0;
}
int battle_delay_damage(unsigned long tick, struct block_list &src, struct block_list &target, int damage, int flag)
{
	struct delay_damage *dat;
	if(!battle_config.delay_battle_damage)
	{
		battle_damage(&src, &target, damage, flag);
		return 0;
	}
	dat = (struct delay_damage *)aCalloc(1, sizeof(struct delay_damage));
	dat->src = &src;
	dat->target = target.id;
	dat->damage = damage;
	dat->flag = flag;
	add_timer(tick, battle_delay_damage_sub, src.id, (int)dat); //!!
	return 0;
}

// ���ۂ�HP�𑀍�
int battle_damage(struct block_list *bl,struct block_list *target,int damage,int flag)
{
	struct map_session_data *sd = NULL;
	struct status_change *sc_data;
	int i;

	nullpo_retr(0, target); //bl��NULL�ŌĂ΂�邱�Ƃ�����̂ő��Ń`�F�b�N
	
	sc_data = status_get_sc_data(target);

	if( damage == 0 ||
		target->prev == NULL ||
		target->type == BL_PET )
		return 0;

	if (bl) {
		if (bl->prev == NULL)
			return 0;
		if (bl->type == BL_PC) {
			nullpo_retr(0, sd = (struct map_session_data *)bl);
		}
	}

	if (damage < 0)
		return battle_heal(bl,target,-damage,0,flag);

	if( !flag && sc_data)
	{
		// �����A�Ή��A����������
		if (sc_data[SC_FREEZE].timer != -1)
			status_change_end(target,SC_FREEZE,-1);
		if (sc_data[SC_STONE].timer!=-1 && sc_data[SC_STONE].val2 == 0)
			status_change_end(target,SC_STONE,-1);
		if (sc_data[SC_SLEEP].timer != -1)
			status_change_end(target,SC_SLEEP,-1);
	}

	if (target->type == BL_MOB) {	// MOB
		struct mob_data *md = (struct mob_data *)target;
		if (md->skilltimer != -1 && md->state.skillcastcancel)	// �r���W�Q
			skill_castcancel(target,0);
		return mob_damage(*md,damage,0,bl);
	} else if (target->type == BL_PC) {	// PC
		struct map_session_data *tsd = (struct map_session_data *)target;
		if (!tsd)
			return 0;
		if(sc_data && sc_data[SC_DEVOTION].val1) {	// �f�B�{�[�V�������������Ă���
			struct map_session_data *sd2 = map_id2sd(tsd->sc_data[SC_DEVOTION].val1);
			if (sd2 && skill_devotion3(&sd2->bl, target->id)) {
				skill_devotion(sd2, target->id);
			} else if (sd2 && bl) {
				for (i = 0; i < 5; i++)
					if (sd2->dev.val1[i] == target->id) {
						clif_damage(*bl, sd2->bl, gettick(), 0, 0, damage, 0 , 0, 0);
						pc_damage(*sd2, damage,&sd2->bl);
						return 0;
					}
			}
		}

		if(tsd && tsd->skilltimer!=-1){	// �r���W�Q
			// �t�F���J�[�h��W�Q����Ȃ��X�L�����̌���
			if( (!tsd->state.no_castcancel || map[bl->m].flag.gvg) && tsd->state.skillcastcancel &&
				!tsd->state.no_castcancel2)
				skill_castcancel(target,0);
		}
		return pc_damage(*tsd,damage,bl);
	} else if (target->type == BL_SKILL)
		return skill_unit_ondamaged((struct skill_unit *)target, bl, damage, gettick());
	return 0;
}

int battle_heal(struct block_list *bl,struct block_list *target,int hp,int sp,int flag)
{
	nullpo_retr(0, target); //bl��NULL�ŌĂ΂�邱�Ƃ�����̂ő��Ń`�F�b�N

	if (target->type == BL_PET)
		return 0;
	if (target->type == BL_PC && pc_isdead(*((struct map_session_data *)target)) )
		return 0;
	if (hp == 0 && sp == 0)
		return 0;

	if (hp < 0)
		return battle_damage(bl,target,-hp,flag);

	if (target->type == BL_MOB)
		return mob_heal(*((struct mob_data *)target),hp);
	else if (target->type == BL_PC)
		return pc_heal(*((struct map_session_data *)target),hp,sp);
	return 0;
}

// �U����~
int battle_stopattack(struct block_list *bl)
{
	nullpo_retr(0, bl);
	if (bl->type == BL_MOB)
		return mob_stopattack( *((struct mob_data*)bl) );
	else if (bl->type == BL_PC)
		return pc_stopattack( *((struct map_session_data*)bl) );
	else if (bl->type == BL_PET)
		return pet_stopattack(*((struct pet_data*)bl));
	return 0;
}
// �ړ���~
int battle_stopwalking(struct block_list *bl,int type)
{
	nullpo_retr(0, bl);
	if (bl->type == BL_MOB)
		return mob_stop_walking(*((struct mob_data*)bl),type);
	else if (bl->type == BL_PC)
		return pc_stop_walking(*((struct map_session_data*)bl),type);
	else if (bl->type == BL_PET)
		return pet_stop_walking(*((struct pet_data*)bl),type);
	return 0;
}


/*==========================================
 * �_���[�W�̑����C��
 *------------------------------------------
 */
int battle_attr_fix(int damage,int atk_elem,int def_elem)
{
	int def_type = def_elem % 10, def_lv = def_elem / 10 / 2;

	if (atk_elem < 0 || atk_elem > 9)
		atk_elem = rand()%9;	//���푮�������_���ŕt��

	//if (def_type < 0 || def_type > 9)
		//def_type = rand()%9;	// change ��������? // celest

	if (def_type < 0 || def_type > 9 ||
		def_lv < 1 || def_lv > 4) {	// �� ���l�����������̂łƂ肠�������̂܂ܕԂ�
		if (battle_config.error_log)
			ShowMessage("battle_attr_fix: unknown attr type: atk=%d def_type=%d def_lv=%d\n",atk_elem,def_type,def_lv);
		return damage;
	}

	return damage*attr_fix_table[def_lv-1][atk_elem][def_type]/100;
}


/*==========================================
 * �_���[�W�ŏI�v�Z
 *------------------------------------------
 */
int battle_calc_damage(struct block_list *src,struct block_list *bl,int damage,int div_,int skill_num,short skill_lv,int flag)
{
	struct map_session_data *sd = NULL;
	struct mob_data *md = NULL;
	struct status_change *sc_data, *sc;
	int class_;

	nullpo_retr(0, bl);
	if(src->m != bl->m) // [ShAPoNe] Src and target same map check.
		return 0;

	class_ = status_get_class(bl);
	if(bl->type==BL_MOB) md=(struct mob_data *)bl;
	else sd=(struct map_session_data *)bl;

	sc_data = status_get_sc_data(bl);

	if(sc_data)
	{
		if (sc_data[SC_SAFETYWALL].timer!=-1 && damage>0 && flag&BF_WEAPON &&
			flag&BF_SHORT && skill_num != NPC_GUIDEDATTACK) {
			// �Z�[�t�e�B�E�H�[��
			struct skill_unit *unit;
			unit = (struct skill_unit *)sc_data[SC_SAFETYWALL].val2;
			// temporary check to prevent access on wrong val2
			if (unit && unit->bl.m == bl->m) {
				if (unit->group && (--unit->group->val2)<=0)
					skill_delunit(unit);
				damage=0;
			} else {
				status_change_end(bl,SC_SAFETYWALL,-1);
			}
		}
		if(sc_data[SC_PNEUMA].timer!=-1 && damage>0 &&
			((flag&BF_WEAPON && flag&BF_LONG && skill_num != NPC_GUIDEDATTACK) ||
			(flag&BF_MISC && (skill_num ==  HT_BLITZBEAT || skill_num == SN_FALCONASSAULT)) ||
			(flag&BF_MAGIC && skill_num == ASC_BREAKER))){ // [DracoRPG]
			// �j���[�}
			damage=0;
		}

		if(sc_data[SC_ROKISWEIL].timer!=-1 && damage>0 &&
			flag&BF_MAGIC ){
			// �j���[�}
			damage=0;
		}

		if(sc_data[SC_AETERNA].timer!=-1 && damage>0){	// ���b�N�X�G�[�e���i
			damage<<=1;
			status_change_end( bl,SC_AETERNA,-1 );
		}

		//������̃_���[�W����
		if(sc_data[SC_VOLCANO].timer!=-1){	// �{���P�[�m
			if(flag&BF_SKILL && skill_get_pl(skill_num)==3)
				//damage += damage*sc_data[SC_VOLCANO].val4/100;
				damage += damage * enchant_eff[sc_data[SC_VOLCANO].val1-1] /100;
			else if(!(flag&BF_SKILL) && status_get_attack_element(bl)==3)
				//damage += damage*sc_data[SC_VOLCANO].val4/100;
				damage += damage * enchant_eff[sc_data[SC_VOLCANO].val1-1] /100;
		}

		if(sc_data[SC_VIOLENTGALE].timer!=-1){	// �o�C�I�����g�Q�C��
			if(flag&BF_SKILL && skill_get_pl(skill_num)==4)
				//damage += damage*sc_data[SC_VIOLENTGALE].val4/100;
				damage += damage * enchant_eff[sc_data[SC_VIOLENTGALE].val1-1] /100;
			else if(!(flag&BF_SKILL) && status_get_attack_element(bl)==4)
				//damage += damage*sc_data[SC_VIOLENTGALE].val4/100;
				damage += damage * enchant_eff[sc_data[SC_VIOLENTGALE].val1-1] /100;
		}

		if(sc_data[SC_DELUGE].timer!=-1){	// �f�����[�W
			if(flag&BF_SKILL && skill_get_pl(skill_num)==1)
				//damage += damage*sc_data[SC_DELUGE].val4/100;
				damage += damage * enchant_eff[sc_data[SC_DELUGE].val1-1] /100;
			else if(!(flag&BF_SKILL) && status_get_attack_element(bl)==1)
				//damage += damage*sc_data[SC_DELUGE].val4/100;
				damage += damage * enchant_eff[sc_data[SC_DELUGE].val1-1] /100;
		}

		if(sc_data[SC_ENERGYCOAT].timer!=-1 && damage>0  && flag&BF_WEAPON){	// �G�i�W�[�R�[�g
			if(sd){
				if(sd->status.sp>0){
					int per = sd->status.sp * 5 / (sd->status.max_sp + 1);
					sd->status.sp -= sd->status.sp * (per * 5 + 10) / 1000;
					if( sd->status.sp < 0 ) sd->status.sp = 0;
					damage -= damage * ((per+1) * 6) / 100;
					clif_updatestatus(*sd,SP_SP);
				}
				if(sd->status.sp<=0)
					status_change_end( bl,SC_ENERGYCOAT,-1 );
			}
			else
				damage -= damage * (sc_data[SC_ENERGYCOAT].val1 * 6) / 100;
		}

		if(sc_data[SC_KYRIE].timer!=-1 && damage > 0){	// �L���G�G���C�\��
			sc=&sc_data[SC_KYRIE];
			sc->val2-=damage;
			if(flag&BF_WEAPON){
				if(sc->val2>=0)	damage=0;
				else damage=-sc->val2;
			}
			if((--sc->val3)<=0 || (sc->val2<=0) || skill_num == AL_HOLYLIGHT)
				status_change_end(bl, SC_KYRIE, -1);
		}

		if(sc_data[SC_BASILICA].timer!=-1 && damage > 0){
			// �j���[�}
			damage=0;
		}
		if(sc_data[SC_LANDPROTECTOR].timer!=-1 && damage>0 && flag&BF_MAGIC){
			// �j���[�}
			damage=0;
		}

		if(sc_data[SC_AUTOGUARD].timer != -1 && damage > 0 && flag&BF_WEAPON) {
			if(rand()%100 < sc_data[SC_AUTOGUARD].val2) {
				int delay;

				damage = 0;
				clif_skill_nodamage(*bl,*bl,CR_AUTOGUARD,(unsigned short)sc_data[SC_AUTOGUARD].val1,1);
				// different delay depending on skill level [celest]
				if (sc_data[SC_AUTOGUARD].val1 <= 5)
					delay = 300;
				else if (sc_data[SC_AUTOGUARD].val1 > 5 && sc_data[SC_AUTOGUARD].val1 <= 9)
					delay = 200;
				else
					delay = 100;
				if(sd)
					sd->canmove_tick = gettick() + delay;
				else if(md)
					md->canmove_tick = gettick() + delay;
			}
		}
// -- moonsoul (chance to block attacks with new Lord Knight skill parrying)
//
		if(sc_data[SC_PARRYING].timer != -1 && damage > 0 && flag&BF_WEAPON) {
			if(rand()%100 < sc_data[SC_PARRYING].val2) {
				damage = 0;
				clif_skill_nodamage(*bl,*bl,LK_PARRYING,(unsigned short)sc_data[SC_PARRYING].val1,1);
			}
		}
		// ���W�F�N�g�\�[�h
		if(sc_data[SC_REJECTSWORD].timer!=-1 && damage > 0 && flag&BF_WEAPON &&
			// Fixed the condition check [Aalye]
			(src->type==BL_MOB || (src->type==BL_PC && (((struct map_session_data *)src)->status.weapon == 1 ||
			((struct map_session_data *)src)->status.weapon == 2 ||
			((struct map_session_data *)src)->status.weapon == 3)))){
			if(rand()%100 < (15*sc_data[SC_REJECTSWORD].val1)){ //���ˊm����15*Lv
				damage = damage*50/100;
				clif_damage(*bl,*src,gettick(),0,0,damage,0,0,0);
				battle_damage(bl,src,damage,0);
				//�_���[�W��^�����̂͗ǂ��񂾂��A��������ǂ����ĕ\������񂾂��킩��˂�
				//�G�t�F�N�g������ł����̂��킩��˂�
				clif_skill_nodamage(*bl,*bl,ST_REJECTSWORD,(unsigned short)sc_data[SC_REJECTSWORD].val1,1);
				if((--sc_data[SC_REJECTSWORD].val2)<=0)
					status_change_end(bl, SC_REJECTSWORD, -1);
			}
		}
		if(sc_data[SC_SPIDERWEB].timer!=-1 && damage > 0)	// [Celest]
			if ((flag&BF_SKILL && skill_get_pl(skill_num)==3) ||
				(!(flag&BF_SKILL) && status_get_attack_element(src)==3)) {
				damage<<=1;
				status_change_end(bl, SC_SPIDERWEB, -1);
			}

		if(sc_data[SC_FOGWALL].timer != -1 && flag&BF_MAGIC)
			if(rand()%100 < 75)
				damage = 0;
	}

	if(class_ == 1288 || class_ == 1287 || class_ == 1286 || class_ == 1285) {
		if(class_ == 1288 && (flag&BF_SKILL || skill_num == ASC_BREAKER || skill_num == PA_SACRIFICE))
			damage=0;
		if(src->type == BL_PC) {
			struct guild *g=guild_search(((struct map_session_data *)src)->status.guild_id);
			struct guild_castle *gc=guild_mapname2gc(map[bl->m].mapname);
			if(!((struct map_session_data *)src)->status.guild_id)
				damage=0;
			if(gc && agit_flag==0 && class_ != 1288)	// guardians cannot be damaged during non-woe [Valaris]
				damage=0;  // end woe check [Valaris]
			if(g == NULL)
				damage=0;//�M���h�������Ȃ�_���[�W����
			else if(g && gc && guild_isallied(*g, *gc))
				damage=0;//����̃M���h�̃G���y�Ȃ�_���[�W����
			else if(g && guild_checkskill(*g,GD_APPROVAL) <= 0)
				damage=0;//���K�M���h���F���Ȃ��ƃ_���[�W����
			else if (g && battle_config.guild_max_castles != 0 && guild_checkcastles(*g)>=battle_config.guild_max_castles)
				damage = 0; // [MouseJstr]
			else if (g && gc && guild_check_alliance(gc->guild_id, g->guild_id, 0) == 1)
				return 0;
		}
		else damage = 0;
	}

	if (damage > 0) { // damage reductions
		if (map[bl->m].flag.gvg) { //GvG
			if (bl->type == BL_MOB){	//defense������΃_���[�W������炵���H
			struct guild_castle *gc=guild_mapname2gc(map[bl->m].mapname);
				if (gc) damage -= damage * (gc->defense / 100) * (battle_config.castle_defense_rate/100);
			}
			if (flag & BF_WEAPON) {
				if (flag & BF_SHORT)
					damage = damage * battle_config.gvg_short_damage_rate/100;
				if (flag & BF_LONG)
					damage = damage * battle_config.gvg_long_damage_rate/100;
			}
			if (flag&BF_MAGIC)
				damage = damage * battle_config.gvg_magic_damage_rate/100;
			if (flag&BF_MISC)
				damage = damage * battle_config.gvg_misc_damage_rate/100;
		} else if (battle_config.pk_mode && bl->type == BL_PC) {
			if (flag & BF_WEAPON) {
				if (flag & BF_SHORT)
					damage = damage * 80/100;
				if (flag & BF_LONG)
					damage = damage * 70/100;
			}
			if (flag & BF_MAGIC)
				damage = damage * 60/100;
			if(flag & BF_MISC)
				damage = damage * 60/100;
		}
		if(damage < 1) damage  = 1;
	}

	if(battle_config.skill_min_damage || flag&BF_MISC) {
		if(div_ < 255) {
			if(damage > 0 && damage < div_)
				damage = div_;
		}
		else if(damage > 0 && damage < 3)
			damage = 3;
	}

	if( md!=NULL && md->hp>0 && damage > 0 )	// �����Ȃǂ�MOB�X�L������
		mobskill_event(*md,flag);

	return damage;
}

/*==========================================
 * HP/SP�z���̌v�Z
 *------------------------------------------
 */
int battle_calc_drain(int damage, int rate, int per, int val)
{
	int diff = 0;

	if (damage <= 0 || rate <= 0)
		return 0;

	if (per && rand()%100 < rate) {
		diff = (damage * per) / 100;
		if (diff == 0) {
			if (per > 0)
				diff = 1;
			else
				diff = -1;
		}
	}

	if (val && rand()%100 < rate) {
		diff += val;
	}
	return diff;
}

/*==========================================
 * �C���_���[�W
 *------------------------------------------
 */
int battle_addmastery(struct map_session_data *sd,struct block_list *target,int damage,int type)
{
	int skill;
	int race=status_get_race(target);
	int weapon;


	nullpo_retr(0, sd);

	// �f�[�����x�C��(+3 �` +30) vs �s�� or ���� (���l�͊܂߂Ȃ��H)
	if((skill = pc_checkskill(*sd,AL_DEMONBANE)) > 0 && (battle_check_undead(race,status_get_elem_type(target)) || race==6) )
		damage += (skill*(int)(3+(sd->status.base_level+1)*0.05));	// submitted by orn
		//damage += (skill * 3);

	// �r�[�X�g�x�C��(+4 �` +40) vs ���� or ����
	if((skill = pc_checkskill(*sd,HT_BEASTBANE)) > 0 && (race==2 || race==4) )
		damage += (skill * 4);

	if(type == 0)
		weapon = sd->weapontype1;
	else
		weapon = sd->weapontype2;
	switch(weapon)
	{
		case 0x01:	// �Z�� Knife
		case 0x02:	// 1HS
		{
			// ���C��(+4 �` +40) �Ў茕 �Z���܂�
			if((skill = pc_checkskill(*sd,SM_SWORD)) > 0) {
				damage += (skill * 4);
			}
			break;
		}
		case 0x03:	// 2HS
		{
			// ���茕�C��(+4 �` +40) ���茕
			if((skill = pc_checkskill(*sd,SM_TWOHAND)) > 0) {
				damage += (skill * 4);
			}
			break;
		}
		case 0x04:	// 1HL
		case 0x05:	// 2HL
		{
			// ���C��(+4 �` +40,+5 �` +50) ��
			if((skill = pc_checkskill(*sd,KN_SPEARMASTERY)) > 0) {
				if(!pc_isriding(*sd))
					damage += (skill * 4);	// �y�R�ɏ���ĂȂ�
				else
					damage += (skill * 5);	// �y�R�ɏ���Ă�
			}
			break;
		}
		case 0x06: // �Ў蕀
		case 0x07: // Axe by Tato
		{
			if((skill = pc_checkskill(*sd,AM_AXEMASTERY)) > 0) {
				damage += (skill * 3);
			}
			break;
		}
		case 0x08:	// ���C�X
		{
			// ���C�X�C��(+3 �` +30) ���C�X
			if((skill = pc_checkskill(*sd,PR_MACEMASTERY)) > 0) {
				damage += (skill * 3);
			}
			break;
		}
		case 0x09:	// �Ȃ�?
			break;
		case 0x0a:	// ��
			break;
		case 0x0b:	// �|
			break;
		case 0x00:	// �f�� Bare Hands
		case 0x0c:	// Knuckles
		{
			// �S��(+3 �` +30) �f��,�i�b�N��
			if((skill = pc_checkskill(*sd,MO_IRONHAND)) > 0) {
				damage += (skill * 3);
			}
			break;
		}
		case 0x0d:	// Musical Instrument
		{
			// �y��̗��K(+3 �` +30) �y��
			if((skill = pc_checkskill(*sd,BA_MUSICALLESSON)) > 0) {
				damage += (skill * 3);
			}
			break;
		}
		case 0x0e:	// Dance Mastery
		{
			// Dance Lesson Skill Effect(+3 damage for every lvl = +30) ��
			if((skill = pc_checkskill(*sd,DC_DANCINGLESSON)) > 0) {
				damage += (skill * 3);
			}
			break;
		}
		case 0x0f:	// Book
		{
			// Advance Book Skill Effect(+3 damage for every lvl = +30) {
			if((skill = pc_checkskill(*sd,SA_ADVANCEDBOOK)) > 0) {
				damage += (skill * 3);
			}
			break;
		}
		case 0x10:	// Katars
		{
			//Advanced Katar Research by zanetheinsane
			if( (skill = pc_checkskill(*sd,ASC_KATAR)) > 0 )
				damage += damage*(10+(skill * 2))/100;

			// �J�^�[���C��(+3 �` +30) �J�^�[��
			if((skill = pc_checkskill(*sd,AS_KATAR)) > 0) {
				//�\�j�b�N�u���[���͕ʏ����i1���ɕt��1/8�K��)
				damage += (skill * 3);
			}
			break;
		}
	}
	return (damage);
}

static struct Damage battle_calc_pet_weapon_attack(
	struct block_list *src,struct block_list *target,int skill_num,int skill_lv,int wflag)
{
	struct pet_data *pd = (struct pet_data *)src;
	struct mob_data *tmd=NULL;
	int hitrate,flee,cri = 0,atkmin,atkmax;
	int luk;
	unsigned int target_count = 1;
	int def1 = status_get_def(target);
	int def2 = status_get_def2(target);
	int t_vit = status_get_vit(target);
	struct Damage wd;
	int damage,damage2=0,type,div_,blewcount=skill_get_blewcount(skill_num,skill_lv);
	int flag,dmg_lv=0;
	int t_mode=0,t_race=0,t_size=1,s_race=0,s_ele=0;
	struct status_change *t_sc_data;
	int ignore_def_flag = 0;
	int div_flag=0;	// 0: total damage is to be divided by div_
					// 1: damage is distributed,and has to be multiplied by div_ [celest]

	//return�O�̏���������̂ŏ��o�͕��̂ݕύX
	if( target == NULL || pd == NULL ){ //src�͓��e�ɒ��ڐG��Ă��Ȃ��̂ŃX���[���Ă݂�
		nullpo_info(NLP_MARK);
		memset(&wd,0,sizeof(wd));
		return wd;
	}

	s_race=status_get_race(src);
	s_ele=status_get_attack_element(src);

	// �^�[�Q�b�g
	if(target->type == BL_MOB)
		tmd=(struct mob_data *)target;
	else {
		memset(&wd,0,sizeof(wd));
		return wd;
	}
	t_race=status_get_race( target );
	t_size=status_get_size( target );
	t_mode=status_get_mode( target );
	t_sc_data=status_get_sc_data( target );

	flag=BF_SHORT|BF_WEAPON|BF_NORMAL;	// �U���̎�ނ̐ݒ�

	// ��𗦌v�Z�A��𔻒�͌��
	flee = status_get_flee(target);
	if(battle_config.agi_penalty_type > 0 || battle_config.vit_penalty_type > 0)
		target_count += battle_counttargeted(*target,src,battle_config.agi_penalty_count_lv);
	if(battle_config.agi_penalty_type > 0) {
		if(target_count >= battle_config.agi_penalty_count) {
			if(battle_config.agi_penalty_type == 1)
				flee = (flee * (100 - (target_count - (battle_config.agi_penalty_count - 1))*battle_config.agi_penalty_num))/100;
			else if(battle_config.agi_penalty_type == 2)
				flee -= (target_count - (battle_config.agi_penalty_count - 1))*battle_config.agi_penalty_num;
			if(flee < 1) flee = 1;
		}
	}
	hitrate = status_get_hit(src) - flee + 80;

	type=0;	// normal
	if (skill_num > 0) {
		div_ = skill_get_num(skill_num,skill_lv);
		if (div_ < 1) div_ = 1;	//Avoid the rare case where the db says div_ is 0 and below
	}
	else div_ = 1; // single attack

	luk=status_get_luk(src);

	if(battle_config.pet_str)
		damage = status_get_baseatk(src);
	else
		damage = 0;

	if(skill_num==HW_MAGICCRASHER){			/* �}�W�b�N�N���b�V���[��MATK�ŉ��� */
		atkmin = status_get_matk1(src);
		atkmax = status_get_matk2(src);
	}else{
	atkmin = status_get_atk(src);
	atkmax = status_get_atk2(src);
	}
	if(mob_db[pd->class_].range>3 )
		flag=(flag&~BF_RANGEMASK)|BF_LONG;

	if(atkmin > atkmax) atkmin = atkmax;

	cri = status_get_critical(src);
	cri -= status_get_luk(target) * 2; // luk/5*10 => target_luk*2 not target_luk*3
	if(battle_config.enemy_critical_rate != 100) {
		cri = cri*battle_config.enemy_critical_rate/100;
		if(cri < 1)
			cri = 1;
	}
	if(t_sc_data) {
		if (t_sc_data[SC_SLEEP].timer!=-1)
			cri <<=1;
		if(t_sc_data[SC_JOINTBEAT].timer != -1 &&
			t_sc_data[SC_JOINTBEAT].val2 == 5) // Always take crits with Neck broken by Joint Beat [DracoRPG]
			cri = 1000;
	}

	if(skill_num == 0 && battle_config.enemy_critical && (rand() % 1000) < cri)
	{
		damage += atkmax;
		type = 0x0a;
	}
	else {
		int vitbonusmax;

		if(atkmax > atkmin)
			damage += atkmin + rand() % (atkmax-atkmin + 1);
		else
			damage += atkmin ;
		// �X�L���C���P�i�U���͔{���n�j
		// �I�[�o�[�g���X�g(+5% �` +25%),���U���n�X�L���̏ꍇ�����ŕ␳
		// �o�b�V��,�}�O�i���u���C�N,
		// �{�[�����O�o�b�V��,�X�s�A�u�[������,�u�����f�B�b�V���X�s�A,�X�s�A�X�^�b�u,
		// ���}�[�i�C�g,�J�[�g���{�����[�V����
		// �_�u���X�g���C�t�B���O,�A���[�V�����[,�`���[�W�A���[,
		// �\�j�b�N�u���[
		if(skill_num>0){
			int i;
			if( (i=skill_get_pl(skill_num))>0 )
				s_ele=i;

			div_=skill_get_num(skill_num,skill_lv); //[Skotlex]
			flag=(flag&~BF_SKILLMASK)|BF_SKILL;
			switch( skill_num ){
			case SM_BASH:		// �o�b�V��
				damage = damage*(100+ 30*skill_lv)/100;
				hitrate = (hitrate*(100+5*skill_lv))/100;
				break;
			case SM_MAGNUM:		// �}�O�i���u���C�N
				damage = damage*(wflag > 1 ? 5*skill_lv+115 : 30*skill_lv+100)/100;
				hitrate = (hitrate*(100+10*skill_lv))/100;
				break;
			case MC_MAMMONITE:	// ���}�[�i�C�g
				damage = damage*(100+ 50*skill_lv)/100;
				break;
			case AC_DOUBLE:	// �_�u���X�g���C�t�B���O
				damage = damage*(180+ 20*skill_lv)/100;
				flag=(flag&~BF_RANGEMASK)|BF_LONG;
				break;
			case AC_SHOWER:	// �A���[�V�����[
				damage = damage*(75 + 5*skill_lv)/100;
				flag=(flag&~BF_RANGEMASK)|BF_LONG;
				break;
			case AC_CHARGEARROW:	// �`���[�W�A���[
				damage = damage*150/100;
				flag=(flag&~BF_RANGEMASK)|BF_LONG;
				break;
			case KN_AUTOCOUNTER:
				ignore_def_flag = 1;
				break;
			case KN_PIERCE:	// �s�A�[�X
				damage = damage*(100+ 10*skill_lv)/100;
				hitrate = hitrate*(100+5*skill_lv)/100;
				div_=t_size+1;
				div_flag = 1;
				break;
			case KN_SPEARSTAB:	// �X�s�A�X�^�u
				damage = damage*(100+ 15*skill_lv)/100;
				break;
			case KN_SPEARBOOMERANG:	// �X�s�A�u�[������
				damage = damage*(100+ 50*skill_lv)/100;
				flag=(flag&~BF_RANGEMASK)|BF_LONG;
				break;
			case KN_BRANDISHSPEAR: // �u�����f�B�b�V���X�s�A
				damage = damage*(100+ 20*skill_lv)/100;
				if(skill_lv>3 && wflag==1) damage2+=damage/2;
				if(skill_lv>6 && wflag==1) damage2+=damage/4;
				if(skill_lv>9 && wflag==1) damage2+=damage/8;
				if(skill_lv>6 && wflag==2) damage2+=damage/2;
				if(skill_lv>9 && wflag==2) damage2+=damage/4;
				if(skill_lv>9 && wflag==3) damage2+=damage/2;
				damage +=damage2;
				break;
			case KN_BOWLINGBASH:	// �{�E�����O�o�b�V��
				damage = damage*(100+ 50*skill_lv)/100;
				blewcount=0;
				break;
			case AS_GRIMTOOTH:
				damage = damage*(100+ 20*skill_lv)/100;
				break;
			case AS_POISONREACT: // celest
				s_ele = 0;
				damage = damage*(100+ 30*skill_lv)/100;
				break;
			case AS_SONICBLOW:	// �\�j�b�N�u���E
				damage = damage*(300+ 50*skill_lv)/100;
				break;
			case TF_SPRINKLESAND:	// ���܂�
				damage = damage*125/100;
				break;
			case MC_CARTREVOLUTION:	// �J�[�g���{�����[�V����
				damage = (damage*150)/100;
				break;
			// �ȉ�MOB
			case NPC_COMBOATTACK:	// ���i�U��
				div_flag = 1;
				break;
			case NPC_RANDOMATTACK:	// �����_��ATK�U��
				damage = damage*(50+rand()%150)/100;
				break;
			// �����U���i�K���j
			case NPC_WATERATTACK:
			case NPC_GROUNDATTACK:
			case NPC_FIREATTACK:
			case NPC_WINDATTACK:
			case NPC_POISONATTACK:
			case NPC_HOLYATTACK:
			case NPC_DARKNESSATTACK:
			case NPC_UNDEADATTACK:
			case NPC_TELEKINESISATTACK:
//				div_= pd->skillduration; // [Valaris]
				break;
			case NPC_GUIDEDATTACK:
				hitrate = 1000000;
				break;
			case NPC_RANGEATTACK:
				flag=(flag&~BF_RANGEMASK)|BF_LONG;
				break;
			case NPC_PIERCINGATT:
				flag=(flag&~BF_RANGEMASK)|BF_SHORT;
				break;
			case NPC_CRITICALSLASH:
				ignore_def_flag = 1;
				break;
			case RG_BACKSTAP:	// �o�b�N�X�^�u
				damage = damage*(300+ 40*skill_lv)/100;
				hitrate = 1000000;
				break;
			case RG_RAID:	// �T�v���C�Y�A�^�b�N
				damage = damage*(100+ 40*skill_lv)/100;
				break;
			case RG_INTIMIDATE:	// �C���e�B�~�f�C�g
				damage = damage*(100+ 30*skill_lv)/100;
				break;
			case CR_SHIELDCHARGE:	// �V�[���h�`���[�W
				damage = damage*(100+ 20*skill_lv)/100;
				flag=(flag&~BF_RANGEMASK)|BF_SHORT;
				s_ele = 0;
				break;
			case CR_SHIELDBOOMERANG:	// �V�[���h�u�[������
				damage = damage*(100+ 30*skill_lv)/100;
				flag=(flag&~BF_RANGEMASK)|BF_LONG;
				s_ele = 0;
				break;
			case CR_HOLYCROSS:	// �z�[���[�N���X
				damage = damage*(100+ 35*skill_lv)/100;
				break;
			case CR_GRANDCROSS:
				hitrate= 1000000;
				break;
			case AM_DEMONSTRATION:	// �f�����X�g���[�V����
				hitrate = 1000000;
				damage = damage*(100+ 20*skill_lv)/100;
				damage2 = damage2*(100+ 20*skill_lv)/100;
				break;
			case AM_ACIDTERROR:	// �A�V�b�h�e���[
				hitrate = 1000000;
				ignore_def_flag = 1;
				damage = damage*(100+ 40*skill_lv)/100;
				damage2 = damage2*(100+ 40*skill_lv)/100;
				break;
			case MO_FINGEROFFENSIVE:	//�w�e
				damage = damage * (125 + 25 * skill_lv) / 100;
				flag=(flag&~BF_RANGEMASK)|BF_LONG;   //orn
				break;
			case MO_INVESTIGATE:	// �� ��
				if(def1 < 1000000)
					damage = damage*(100+ 75*skill_lv)/100 * (def1 + def2)/50;
				hitrate = 1000000;
				ignore_def_flag = 1;
				s_ele = 0;
				break;
			case MO_EXTREMITYFIST:	// ���C���e�P��
				damage = damage * 8 + 250 + (skill_lv * 150);
				hitrate = 1000000;
				ignore_def_flag = 1;
				s_ele = 0;
				break;
			case MO_CHAINCOMBO:	// �A�ŏ�
				damage = damage*(150+ 50*skill_lv)/100;
				break;
			case MO_COMBOFINISH:	// �җ���
				damage = damage*(240+ 60*skill_lv)/100;
				break;
			case DC_THROWARROW:	// ���
				damage = damage*(60+ 40 * skill_lv)/100;
				flag=(flag&~BF_RANGEMASK)|BF_LONG;
				break;
			case BA_MUSICALSTRIKE:	// �~���[�W�J���X�g���C�N
				damage = damage*(60+ 40 * skill_lv)/100;
				flag=(flag&~BF_RANGEMASK)|BF_LONG;
				break;
			case CH_TIGERFIST:	// ���Ռ�
				damage = damage*(40+ 100*skill_lv)/100;
				break;
			case CH_CHAINCRUSH:	// �A������
				damage = damage*(400+ 100*skill_lv)/100;
				break;
			case CH_PALMSTRIKE:	// �ҌՍd�h�R
				damage = damage*(200+ 100*skill_lv)/100;
				break;
			case LK_SPIRALPIERCE:			/* �X�p�C�����s�A�[�X */
				damage = damage*(100+ 50*skill_lv)/100; //�����ʂ�������Ȃ��̂œK����
				ignore_def_flag = 1;
				flag=(flag&~BF_RANGEMASK)|BF_LONG;
				if(target->type == BL_PC)
					((struct map_session_data *)target)->canmove_tick = gettick() + 1000;
				else if(target->type == BL_MOB)
					((struct mob_data *)target)->canmove_tick = gettick() + 1000;
				break;
			case LK_HEADCRUSH:				/* �w�b�h�N���b�V�� */
				damage = damage*(100+ 40*skill_lv)/100;
				break;
			case LK_JOINTBEAT:				/* �W���C���g�r�[�g */
				damage = damage*(50+ 10*skill_lv)/100;
				break;
			case ASC_METEORASSAULT:			/* ���e�I�A�T���g */
				damage = damage*(40+ 40*skill_lv)/100;
				break;
			case SN_SHARPSHOOTING:			/* �V���[�v�V���[�e�B���O */
				damage += damage*(100+50*skill_lv)/100;
				break;
			case CG_ARROWVULCAN:			/* �A���[�o���J�� */
				damage = damage*(200+100*skill_lv)/100;
				break;
			case AS_SPLASHER:		/* �x�i���X�v���b�V���[ */
				damage = damage*(200+20*skill_lv)/100;
				hitrate = 1000000;
				break;
			case PA_SHIELDCHAIN:	// Shield Chain
				damage = damage*(30*skill_lv)/100;
				flag = (flag&~BF_RANGEMASK)|BF_LONG;
				hitrate += 20;
				div_flag = 1;
				s_ele = 0;				
				break;
			case WS_CARTTERMINATION:
				damage = damage * (80000 / (10 * (16 - skill_lv)) )/100;
				break;
			case CR_ACIDDEMONSTRATION:
				div_flag = 1;
				// damage according to vit and int
				break;
			}
			if (div_flag && div_ > 1) {	// [Skotlex]
				damage *= div_;
				damage2 *= div_;
			}
		}

		// �� �ۂ̖h��͂ɂ��_���[�W�̌���
		// �f�B�o�C���v���e�N�V�����i�����ł����̂��ȁH�j
		if (!ignore_def_flag && def1 < 1000000) {	//DEF, VIT����
			int t_def;
			target_count = 1 + battle_counttargeted(*target,src,battle_config.vit_penalty_count_lv);
			if(battle_config.vit_penalty_type > 0) {
				if(target_count >= battle_config.vit_penalty_count) {
					if(battle_config.vit_penalty_type == 1) {
						def1 = (def1 * (100 - (target_count - (battle_config.vit_penalty_count - 1))*battle_config.vit_penalty_num))/100;
						def2 = (def2 * (100 - (target_count - (battle_config.vit_penalty_count - 1))*battle_config.vit_penalty_num))/100;
						t_vit = (t_vit * (100 - (target_count - (battle_config.vit_penalty_count - 1))*battle_config.vit_penalty_num))/100;
					}
					else if(battle_config.vit_penalty_type == 2) {
						def1 -= (target_count - (battle_config.vit_penalty_count - 1))*battle_config.vit_penalty_num;
						def2 -= (target_count - (battle_config.vit_penalty_count - 1))*battle_config.vit_penalty_num;
						t_vit -= (target_count - (battle_config.vit_penalty_count - 1))*battle_config.vit_penalty_num;
					}
					if(def1 < 0) def1 = 0;
						if(def2 < 1) def2 = 1;
					if(t_vit < 1) t_vit = 1;
				}
			}
			t_def = def2*8/10;
			vitbonusmax = (t_vit/20)*(t_vit/20)-1;
			if(battle_config.pet_defense_type) {
				damage = damage - (def1 * battle_config.pet_defense_type) - t_def - ((vitbonusmax < 1)?0: rand()%(vitbonusmax+1) );
			}
			else{
				damage = damage * (100 - def1) /100 - t_def - ((vitbonusmax < 1)?0: rand()%(vitbonusmax+1) );
			}
			
		}
	}

	// 0�����������ꍇ1�ɕ␳
	if(damage<1) damage=1;

	// ����C��
	if(	hitrate < 1000000 && t_sc_data ) {			// �K���U��
		if(t_sc_data[SC_FOGWALL].timer != -1 && flag&BF_LONG)
			hitrate -= 75;
		if (t_sc_data[SC_SLEEP].timer!=-1 ||	// �����͕K��
			t_sc_data[SC_STAN].timer!=-1 ||		// �X�^���͕K��
			t_sc_data[SC_FREEZE].timer!=-1 ||
			(t_sc_data[SC_STONE].timer!=-1 && t_sc_data[SC_STONE].val2==0))	// �����͕K��
			hitrate = 1000000;
	}
	if(hitrate < 1000000)
		hitrate = ( (hitrate>95)?95: ((hitrate<5)?5:hitrate) );
	if(type == 0 && rand()%100 >= hitrate) {
		damage = damage2 = 0;
		dmg_lv = ATK_FLEE;
	} else {
		dmg_lv = ATK_DEF;
	}


	if(t_sc_data) {
		int cardfix=100;
		if(t_sc_data[SC_DEFENDER].timer != -1 && flag&BF_LONG)
			cardfix=cardfix*(100-t_sc_data[SC_DEFENDER].val2)/100;
		if(t_sc_data[SC_FOGWALL].timer != -1 && flag&BF_LONG)
			cardfix=cardfix*50/100;
		if(cardfix != 100)
			damage=damage*cardfix/100;
	}
	if(damage < 0) damage = 0;

	// �� ���̓K�p
	if(skill_num != 0 || s_ele != 0 || !battle_config.pet_attack_attr_none)
	damage=battle_attr_fix(damage, s_ele, status_get_element(target) );

	if(skill_num==PA_PRESSURE) /* �v���b�V���[ �K��? */
		damage = 500+300*skill_lv;

	// �C���x�i���C��
	if(skill_num==TF_POISON){
		damage = battle_attr_fix(damage + 15*skill_lv, s_ele, status_get_element(target) );
	}
	if(skill_num==MC_CARTREVOLUTION){
		damage = battle_attr_fix(damage, 0, status_get_element(target) );
	}

	// ���S����̔���
	if(battle_config.enemy_perfect_flee) {
		if(skill_num == 0 && tmd!=NULL && rand()%1000 < status_get_flee2(target) ){
			damage=0;
			type=0x0b;
			dmg_lv = ATK_LUCKY;
		}
	}

//	if(def1 >= 1000000 && damage > 0)
	if(t_mode&0x40 && damage > 0)
		damage = 1;

	if(is_boss(target))
		blewcount = 0;

	if(skill_num != CR_GRANDCROSS)
		damage=battle_calc_damage(src,target,damage,div_,skill_num,skill_lv,flag);

	wd.damage=damage;
	wd.damage2=0;
	wd.type=type;
	wd.div_=div_;
	wd.amotion=status_get_amotion(src);
	if(skill_num == KN_AUTOCOUNTER)
		wd.amotion >>= 1;
	wd.dmotion=status_get_dmotion(target);
	wd.blewcount=blewcount;
	wd.flag=flag;
	wd.dmg_lv=dmg_lv;

	return wd;
}

struct Damage battle_calc_mob_weapon_attack(struct block_list *src,struct block_list *target,int skill_num,int skill_lv,int wflag)
{
	struct map_session_data *tsd=NULL;
	struct mob_data* md=(struct mob_data *)src,*tmd=NULL;
	int hitrate,flee,cri = 0,atkmin,atkmax;
	int luk;
	unsigned int target_count = 1;
	int def1 = status_get_def(target);
	int def2 = status_get_def2(target);
	int t_vit = status_get_vit(target);
	struct Damage wd;
	int damage,damage2=0,type,div_,blewcount=skill_get_blewcount(skill_num,skill_lv);
	int flag,skill,ac_flag = 0,dmg_lv = 0;
	int t_mode=0,t_race=0,t_size=1,s_race=0,s_ele=0,s_size=0,s_race2=0;
	struct status_change *sc_data,*t_sc_data;
	short *option, *opt1, *opt2;
	int ignore_def_flag = 0;
	int div_flag=0;	// 0: total damage is to be divided by div_
					// 1: damage is distributed,and has to be multiplied by div_ [celest]

	//return�O�̏���������̂ŏ��o�͕��̂ݕύX
	if( src == NULL || target == NULL || md == NULL ){
		nullpo_info(NLP_MARK);
		memset(&wd,0,sizeof(wd));
		return wd;
	}

	s_race = status_get_race(src);
	s_ele = status_get_attack_element(src);
	s_size = status_get_size(src);
	sc_data = status_get_sc_data(src);
	option = status_get_option(src);
	opt1 = status_get_opt1(src);
	opt2 = status_get_opt2(src);
	s_race2 = status_get_race2(src);

	// �^�[�Q�b�g
	if(target->type == BL_PC)
		tsd = (struct map_session_data *)target;
	else if(target->type == BL_MOB)
		tmd = (struct mob_data *)target;
	t_race = status_get_race( target );
	t_size = status_get_size( target );
	t_mode = status_get_mode( target );
	t_sc_data = status_get_sc_data( target );

	if(skill_num == 0 || (target->type == BL_PC && battle_config.pc_auto_counter_type&2) ||
		(target->type == BL_MOB && battle_config.monster_auto_counter_type&2)) {
		if(skill_num != CR_GRANDCROSS && t_sc_data && t_sc_data[SC_AUTOCOUNTER].timer != -1) {
			int dir = map_calc_dir(*src,target->x,target->y),t_dir = status_get_dir(target);
			int dist = distance(src->x,src->y,target->x,target->y);
			if(dist <= 0 || map_check_dir(dir,t_dir) ) {
				memset(&wd,0,sizeof(wd));
				t_sc_data[SC_AUTOCOUNTER].val3 = 0;
				t_sc_data[SC_AUTOCOUNTER].val4 = 1;
				if(sc_data && sc_data[SC_AUTOCOUNTER].timer == -1) {
					int range = status_get_range(target);
					if((target->type == BL_PC && ((struct map_session_data *)target)->status.weapon != 11 && dist <= range+1) ||
						(target->type == BL_MOB && range <= 3 && dist <= range+1) )
						t_sc_data[SC_AUTOCOUNTER].val3 = src->id;
				}
				return wd;
			}
			else ac_flag = 1;
		} else if(skill_num != CR_GRANDCROSS && t_sc_data && t_sc_data[SC_POISONREACT].timer != -1) {   // poison react [Celest]
			t_sc_data[SC_POISONREACT].val3 = 0;
			t_sc_data[SC_POISONREACT].val4 = 1;
			t_sc_data[SC_POISONREACT].val3 = src->id;
		}
	}
	flag=BF_SHORT|BF_WEAPON|BF_NORMAL;	// �U���̎�ނ̐ݒ�

	// ��𗦌v�Z�A��𔻒�͌��
	flee = status_get_flee(target);
	if(battle_config.agi_penalty_type > 0 || battle_config.vit_penalty_type > 0)
		target_count += battle_counttargeted(*target,src,battle_config.agi_penalty_count_lv);
	if(battle_config.agi_penalty_type > 0) {
		if(target_count >= battle_config.agi_penalty_count) {
			if(battle_config.agi_penalty_type == 1)
				flee = (flee * (100 - (target_count - (battle_config.agi_penalty_count - 1))*battle_config.agi_penalty_num))/100;
			else if(battle_config.agi_penalty_type == 2)
				flee -= (target_count - (battle_config.agi_penalty_count - 1))*battle_config.agi_penalty_num;
			if(flee < 1) flee = 1;
		}
	}
	hitrate=status_get_hit(src) - flee + 80;

	type=0;	// normal
	if (skill_num > 0) {
		div_ = skill_get_num(skill_num,skill_lv);
		if (div_ < 1) div_ = 1;	//Avoid the rare case where the db says div_ is 0 and below
	} else div_ = 1; // single attack

	luk=status_get_luk(src);

	if(battle_config.enemy_str)
		damage = status_get_baseatk(src);
	else
		damage = 0;
	if(skill_num==HW_MAGICCRASHER){			/* �}�W�b�N�N���b�V���[��MATK�ŉ��� */
		atkmin = status_get_matk1(src);
		atkmax = status_get_matk2(src);
	}else{
	atkmin = status_get_atk(src);
	atkmax = status_get_atk2(src);
	}
	if(mob_db[md->class_].range>3 )
		flag=(flag&~BF_RANGEMASK)|BF_LONG;

	if(atkmin > atkmax) atkmin = atkmax;

	if(sc_data != NULL && sc_data[SC_MAXIMIZEPOWER].timer!=-1 ){	// �}�L�V�}�C�Y�p���[
		atkmin=atkmax;
	}

	cri = status_get_critical(src);
	cri -= status_get_luk(target) * 3;
	if(battle_config.enemy_critical_rate != 100) {
		cri = cri*battle_config.enemy_critical_rate/100;
		if(cri < 1)
			cri = 1;
	}
	if(t_sc_data) {
		if (t_sc_data[SC_SLEEP].timer!=-1 )	// �������̓N���e�B�J�����{��
			cri <<=1;
		if(t_sc_data[SC_JOINTBEAT].timer != -1 &&
			t_sc_data[SC_JOINTBEAT].val2 == 5) // Always take crits with Neck broken by Joint Beat [DracoRPG]
			cri = 1000;
	}

	if(ac_flag) cri = 1000;

	if(skill_num == KN_AUTOCOUNTER) {
		if(!(battle_config.monster_auto_counter_type&1))
			cri = 1000;
		else
			cri <<= 1;
	}

	if(tsd && tsd->critical_def)
		cri = cri * (100 - tsd->critical_def) / 100;

	if((skill_num == 0 || skill_num == KN_AUTOCOUNTER) && skill_lv >= 0 && battle_config.enemy_critical && (rand() % 1000) < cri)	// ����i�X�L���̏ꍇ�͖����j
			// �G�̔���
	{
		damage += atkmax;
		type = 0x0a;
	}
	else {
		int vitbonusmax;

		if(atkmax > atkmin)
			damage += atkmin + rand() % (atkmax-atkmin + 1);
		else
			damage += atkmin ;
		// �X�L���C���P�i�U���͔{���n�j
		// �I�[�o�[�g���X�g(+5% �` +25%),���U���n�X�L���̏ꍇ�����ŕ␳
		// �o�b�V��,�}�O�i���u���C�N,
		// �{�[�����O�o�b�V��,�X�s�A�u�[������,�u�����f�B�b�V���X�s�A,�X�s�A�X�^�b�u,
		// ���}�[�i�C�g,�J�[�g���{�����[�V����
		// �_�u���X�g���C�t�B���O,�A���[�V�����[,�`���[�W�A���[,
		// �\�j�b�N�u���[
		if(sc_data){ //��Ԉُ풆�̃_���[�W�ǉ�
			if(sc_data[SC_OVERTHRUST].timer!=-1)	// �I�[�o�[�g���X�g
				damage += damage*(5*sc_data[SC_OVERTHRUST].val1)/100;
			if(sc_data[SC_TRUESIGHT].timer!=-1)	// �g�D���[�T�C�g
				damage += damage*(2*sc_data[SC_TRUESIGHT].val1)/100;
			if(sc_data[SC_BERSERK].timer!=-1)	// �o�[�T�[�N
				damage += damage;
			if(sc_data && sc_data[SC_AURABLADE].timer!=-1)	//[DracoRPG]
				damage += sc_data[SC_AURABLADE].val1 * 20;
			if(sc_data[SC_MAXOVERTHRUST].timer!=-1)
				damage += damage*(20*sc_data[SC_MAXOVERTHRUST].val1)/100;
		}

		if(skill_num>0){
			int i;
			if( (i=skill_get_pl(skill_num))>0 )
				s_ele=i;

			div_=skill_get_num(skill_num,skill_lv); //[Skotlex] div calculation
			flag=(flag&~BF_SKILLMASK)|BF_SKILL;
			switch( skill_num ){
			case SM_BASH:		// �o�b�V��
				damage = damage*(100+ 30*skill_lv)/100;
				hitrate = (hitrate*(100+5*skill_lv))/100;
				break;
			case SM_MAGNUM:		// �}�O�i���u���C�N
				damage = damage*(wflag > 1 ? 5*skill_lv+115 : 30*skill_lv+100)/100;
				hitrate = (hitrate*(100+10*skill_lv))/100;
				break;
			case MC_MAMMONITE:	// ���}�[�i�C�g
				damage = damage*(100+ 50*skill_lv)/100;
				break;
			case AC_DOUBLE:	// �_�u���X�g���C�t�B���O
				damage = damage*(180+ 20*skill_lv)/100;
				flag=(flag&~BF_RANGEMASK)|BF_LONG;
				break;
			case AC_SHOWER:	// �A���[�V�����[
				damage = damage*(75 + 5*skill_lv)/100;
				flag=(flag&~BF_RANGEMASK)|BF_LONG;
				break;
			case AC_CHARGEARROW:	// �`���[�W�A���[
				damage = damage*150/100;
				flag=(flag&~BF_RANGEMASK)|BF_LONG;
				break;
			case KN_PIERCE:	// �s�A�[�X
				damage = damage*(100+ 10*skill_lv)/100;
				hitrate = hitrate*(100+5*skill_lv)/100;
				div_ = t_size+1;
				div_flag = 1;
				break;
			case KN_SPEARSTAB:	// �X�s�A�X�^�u
				damage = damage*(100+ 15*skill_lv)/100;
				break;
			case KN_SPEARBOOMERANG:	// �X�s�A�u�[������
				damage = damage*(100+ 50*skill_lv)/100;
				flag=(flag&~BF_RANGEMASK)|BF_LONG;
				break;
			case KN_BRANDISHSPEAR: // �u�����f�B�b�V���X�s�A
				damage = damage*(100+ 20*skill_lv)/100;
				if(skill_lv>3 && wflag==1) damage2+=damage/2;
				if(skill_lv>6 && wflag==1) damage2+=damage/4;
				if(skill_lv>9 && wflag==1) damage2+=damage/8;
				if(skill_lv>6 && wflag==2) damage2+=damage/2;
				if(skill_lv>9 && wflag==2) damage2+=damage/4;
				if(skill_lv>9 && wflag==3) damage2+=damage/2;
				damage +=damage2;
				break;
			case KN_BOWLINGBASH:	// �{�E�����O�o�b�V��
				damage = damage*(100+ 50*skill_lv)/100;
				blewcount=0;
				break;
			case KN_AUTOCOUNTER:
				if(battle_config.monster_auto_counter_type&1)
					hitrate += 20;
				else
					hitrate = 1000000;
				ignore_def_flag = 1;
				flag=(flag&~BF_SKILLMASK)|BF_NORMAL;
				break;
			case AS_GRIMTOOTH:
				damage = damage*(100+ 20*skill_lv)/100;
				break;
			case AS_POISONREACT: // celest
				s_ele = 0;
				damage = damage*(100+ 30*skill_lv)/100;
				break;
			case AS_SONICBLOW:	// �\�j�b�N�u���E
				damage = damage*(300+ 50*skill_lv)/100;
				break;
			case TF_SPRINKLESAND:	// ���܂�
				damage = damage*125/100;
				break;
			case MC_CARTREVOLUTION:	// �J�[�g���{�����[�V����
				damage = (damage*150)/100;
				break;
			// �ȉ�MOB
			case NPC_COMBOATTACK:	// ���i�U��
				div_flag = 1;
				break;
			case NPC_RANDOMATTACK:	// �����_��ATK�U��
				damage = damage*(50+rand()%150)/100;
				break;
			// �����U���i�K���j
			case NPC_WATERATTACK:
			case NPC_GROUNDATTACK:
			case NPC_FIREATTACK:
			case NPC_WINDATTACK:
			case NPC_POISONATTACK:
			case NPC_HOLYATTACK:
			case NPC_DARKNESSATTACK:
			case NPC_UNDEADATTACK:
			case NPC_TELEKINESISATTACK:
				damage = damage*(100+25*(skill_lv-1))/100;
				break;
			case NPC_GUIDEDATTACK:
				hitrate = 1000000;
				break;
			case NPC_RANGEATTACK:
				flag=(flag&~BF_RANGEMASK)|BF_LONG;
				break;
			case NPC_PIERCINGATT:
				flag=(flag&~BF_RANGEMASK)|BF_SHORT;
				break;
			case NPC_CRITICALSLASH:
				ignore_def_flag = 1;
				break;
			case RG_BACKSTAP:	// �o�b�N�X�^�u
				damage = damage*(300+ 40*skill_lv)/100;
				hitrate = 1000000;
				break;
			case RG_RAID:	// �T�v���C�Y�A�^�b�N
				damage = damage*(100+ 40*skill_lv)/100;
				break;
			case RG_INTIMIDATE:	// �C���e�B�~�f�C�g
				damage = damage*(100+ 30*skill_lv)/100;
				break;
			case CR_SHIELDCHARGE:	// �V�[���h�`���[�W
				damage = damage*(100+ 20*skill_lv)/100;
				flag=(flag&~BF_RANGEMASK)|BF_SHORT;
				s_ele = 0;
				break;
			case CR_SHIELDBOOMERANG:	// �V�[���h�u�[������
				damage = damage*(100+ 30*skill_lv)/100;
				flag=(flag&~BF_RANGEMASK)|BF_LONG;
				s_ele = 0;
				break;
			case CR_HOLYCROSS:	// �z�[���[�N���X
				damage = damage*(100+ 35*skill_lv)/100;
				break;
			case CR_GRANDCROSS:
				hitrate= 1000000;
				break;
			case AM_DEMONSTRATION:	// �f�����X�g���[�V����
				hitrate = 1000000;
				damage = damage*(100+ 20*skill_lv)/100;
				damage2 = damage2*(100+ 20*skill_lv)/100;
				break;
			case AM_ACIDTERROR:	// �A�V�b�h�e���[
				hitrate = 1000000;
				ignore_def_flag = 1;
				damage = damage*(100+ 40*skill_lv)/100;
				damage2 = damage2*(100+ 40*skill_lv)/100;
				break;
			case MO_FINGEROFFENSIVE:	//�w�e
				damage = damage * (125 + 25 * skill_lv) / 100;
				flag=(flag&~BF_RANGEMASK)|BF_LONG;   //orn
				break;
			case MO_INVESTIGATE:	// �� ��
				if(def1 < 1000000)
					damage = damage*(100+ 75*skill_lv)/100 * (def1 + def2)/50;
				hitrate = 1000000;
				ignore_def_flag = 1;
				s_ele = 0;
				break;
			case MO_EXTREMITYFIST:	// ���C���e�P��
				damage = damage * 8 + 250 + (skill_lv * 150);
				hitrate = 1000000;
				ignore_def_flag = 1;
				s_ele = 0;
				break;
			case MO_CHAINCOMBO:	// �A�ŏ�
				damage = damage*(150+ 50*skill_lv)/100;
				break;
			case BA_MUSICALSTRIKE:	// �~���[�W�J���X�g���C�N
				damage = damage*(60+ 40 * skill_lv)/100;
				flag=(flag&~BF_RANGEMASK)|BF_LONG;
				break;
			case DC_THROWARROW:	// ���
				damage = damage*(60+ 40 * skill_lv)/100;
				flag=(flag&~BF_RANGEMASK)|BF_LONG;
				break;
			case MO_COMBOFINISH:	// �җ���
				damage = damage*(240+ 60*skill_lv)/100;
				break;
			case CH_TIGERFIST:	// ���Ռ�
				damage = damage*(40+ 100*skill_lv)/100;
				break;
			case CH_CHAINCRUSH:	// �A������
				damage = damage*(400+ 100*skill_lv)/100;
				break;
			case CH_PALMSTRIKE:	// �ҌՍd�h�R
				damage = damage*(200+ 100*skill_lv)/100;
				break;
			case LK_SPIRALPIERCE:			/* �X�p�C�����s�A�[�X */
				damage = damage*(100+ 50*skill_lv)/100; //�����ʂ�������Ȃ��̂œK����
				ignore_def_flag = 1;
				flag=(flag&~BF_RANGEMASK)|BF_LONG;
				if(tsd)
					tsd->canmove_tick = gettick() + 1000;
				else if(tmd)
					tmd->canmove_tick = gettick() + 1000;
				break;
			case LK_HEADCRUSH:				/* �w�b�h�N���b�V�� */
				damage = damage*(100+ 40*skill_lv)/100;
				break;
			case LK_JOINTBEAT:				/* �W���C���g�r�[�g */
				damage = damage*(50+ 10*skill_lv)/100;
				break;
			case ASC_METEORASSAULT:			/* ���e�I�A�T���g */
				damage = damage*(40+ 40*skill_lv)/100;
				break;
			case SN_SHARPSHOOTING:			/* �V���[�v�V���[�e�B���O */
				damage += damage*(100+50*skill_lv)/100;
				break;
			case CG_ARROWVULCAN:			/* �A���[�o���J�� */
				damage = damage*(200+100*skill_lv)/100;
				break;
			case AS_SPLASHER:		/* �x�i���X�v���b�V���[ */
				damage = damage*(200+20*skill_lv)/100;
				hitrate = 1000000;
				break;
			case PA_SHIELDCHAIN:	// Shield Chain
				damage = damage*(30*skill_lv)/100;
				flag = (flag&~BF_RANGEMASK)|BF_LONG;
				hitrate += 20;
				div_flag = 1;
				s_ele = 0;				
				break;
			case WS_CARTTERMINATION:
				damage = damage * (80000 / (10 * (16 - skill_lv)) )/100;
				break;
			case CR_ACIDDEMONSTRATION:
				div_flag = 1;
				// damage according to vit and int
				break;
			}
			if (div_flag && div_ > 1) {	// [Skotlex]
				damage *= div_;
				damage2 *= div_;
			}
		}

		// �� �ۂ̖h��͂ɂ��_���[�W�̌���
		// �f�B�o�C���v���e�N�V�����i�����ł����̂��ȁH�j
		if (!ignore_def_flag && def1 < 1000000) {	//DEF, VIT����
			int t_def;
			target_count = 1 + battle_counttargeted(*target,src,battle_config.vit_penalty_count_lv);
			if(battle_config.vit_penalty_type > 0) {
				if(target_count >= battle_config.vit_penalty_count) {
					if(battle_config.vit_penalty_type == 1) {
						def1 = (def1 * (100 - (target_count - (battle_config.vit_penalty_count - 1))*battle_config.vit_penalty_num))/100;
						def2 = (def2 * (100 - (target_count - (battle_config.vit_penalty_count - 1))*battle_config.vit_penalty_num))/100;
						t_vit = (t_vit * (100 - (target_count - (battle_config.vit_penalty_count - 1))*battle_config.vit_penalty_num))/100;
					}
					else if(battle_config.vit_penalty_type == 2) {
						def1 -= (target_count - (battle_config.vit_penalty_count - 1))*battle_config.vit_penalty_num;
						def2 -= (target_count - (battle_config.vit_penalty_count - 1))*battle_config.vit_penalty_num;
						t_vit -= (target_count - (battle_config.vit_penalty_count - 1))*battle_config.vit_penalty_num;
					}
					if(def1 < 0) def1 = 0;
					if(def2 < 1) def2 = 1;
					if(t_vit < 1) t_vit = 1;
				}
			}
			t_def = def2*8/10;
			if(battle_check_undead(s_race,status_get_elem_type(src)) || s_race==6)
				if(tsd && (skill=pc_checkskill(*tsd,AL_DP)) > 0 )
					t_def += skill* (int) (3 + (tsd->status.base_level+1)*0.04);	// submitted by orn
					//t_def += skill*3;

			vitbonusmax = (t_vit/20)*(t_vit/20)-1;
			if(battle_config.monster_defense_type) {
				damage = damage - (def1 * battle_config.monster_defense_type) - t_def - ((vitbonusmax < 1)?0: rand()%(vitbonusmax+1) );
			}
			else{
				damage = damage * (100 - def1) /100 - t_def - ((vitbonusmax < 1)?0: rand()%(vitbonusmax+1) );
			}
		}
	}

	// 0�����������ꍇ1�ɕ␳
	if(damage<1) damage=1;

	// ����C��
	if(	hitrate < 1000000 && t_sc_data ) {			// �K���U��
		if(t_sc_data[SC_FOGWALL].timer != -1 && flag&BF_LONG)
			hitrate -= 75;
		if (t_sc_data[SC_SLEEP].timer!=-1 ||	// �����͕K��
			t_sc_data[SC_STAN].timer!=-1 ||		// �X�^���͕K��
			t_sc_data[SC_FREEZE].timer!=-1 ||
			(t_sc_data[SC_STONE].timer!=-1 && t_sc_data[SC_STONE].val2==0))	// �����͕K��
			hitrate = 1000000;
	}
	if(hitrate < 1000000)
		hitrate = ( (hitrate>95)?95: ((hitrate<5)?5:hitrate) );
	if(type == 0 && rand()%100 >= hitrate) {
		damage = damage2 = 0;
		dmg_lv = ATK_FLEE;
	} else {
		dmg_lv = ATK_DEF;
	}

	if(tsd){
		int cardfix=100,i;
		cardfix=cardfix*(100-tsd->subele[s_ele])/100;	// �� ���ɂ��_���[�W�ϐ�
		cardfix=cardfix*(100-tsd->subrace[s_race])/100;	// �푰�ɂ��_���[�W�ϐ�
		cardfix=cardfix*(100-tsd->subsize[s_size])/100;
		cardfix=cardfix*(100-tsd->subrace2[s_race2])/100;	// �푰�ɂ��_���[�W�ϐ�
		if(is_boss(src))
			cardfix=cardfix*(100-tsd->subrace[10])/100;
		else
			cardfix=cardfix*(100-tsd->subrace[11])/100;
		for(i=0;i<tsd->add_def_class_count;i++) {
			if(tsd->add_def_classid[i] == md->class_) {
				cardfix=cardfix*(100-tsd->add_def_classrate[i])/100;
				break;
			}
		}
		for(i=0;i<tsd->add_damage_class_count2;i++) {
			if(tsd->add_damage_classid2[i] == md->class_) {
				cardfix=cardfix*(100+tsd->add_damage_classrate2[i])/100;
				break;
			}
		}
		if(flag&BF_LONG)
			cardfix=cardfix*(100-tsd->long_attack_def_rate)/100;
		if(flag&BF_SHORT)
			cardfix=cardfix*(100-tsd->near_attack_def_rate)/100;
		damage=damage*cardfix/100;
	}
	if(t_sc_data) {
		int cardfix=100;
		if(t_sc_data[SC_DEFENDER].timer != -1 && flag&BF_LONG)
			cardfix=cardfix*(100-t_sc_data[SC_DEFENDER].val2)/100;
		if(t_sc_data[SC_FOGWALL].timer != -1 && flag&BF_LONG)
			cardfix=cardfix*50/100;
		if(cardfix != 100)
			damage=damage*cardfix/100;
	}
	if(t_sc_data && t_sc_data[SC_ASSUMPTIO].timer != -1){ //�A�V�����v�e�B�I
		if(!map[target->m].flag.pvp)
			damage=damage/3;
		else
			damage=damage/2;
	}

	if(damage < 0) damage = 0;

	// �� ���̓K�p
	if (!((battle_config.mob_ghostring_fix == 1) &&
		(status_get_elem_type(target) == 8) &&
		(target->type==BL_PC))) // [MouseJstr]
		if(skill_num != 0 || s_ele != 0 || !battle_config.mob_attack_attr_none)
			damage=battle_attr_fix(damage, s_ele, status_get_element(target) );

	//if(sc_data && sc_data[SC_AURABLADE].timer!=-1)	/* �I�[���u���[�h �K�� */
	//	damage += sc_data[SC_AURABLADE].val1 * 10;
	if(skill_num==PA_PRESSURE) /* �v���b�V���[ �K��? */
		damage = 500+300*skill_lv;

	// �C���x�i���C��
	if(skill_num==TF_POISON){
		damage = battle_attr_fix(damage + 15*skill_lv, s_ele, status_get_element(target) );
	}
	if(skill_num==MC_CARTREVOLUTION){
		damage = battle_attr_fix(damage, 0, status_get_element(target) );
	}

	// ���S����̔���
	if(skill_num == 0 && tsd!=NULL && rand()%1000 < status_get_flee2(target) ){
		damage=0;
		type=0x0b;
		dmg_lv = ATK_LUCKY;
	}

	if(battle_config.enemy_perfect_flee) {
		if(skill_num == 0 && tmd!=NULL && rand()%1000 < status_get_flee2(target) ){
			damage=0;
			type=0x0b;
			dmg_lv = ATK_LUCKY;
		}
	}

//	if(def1 >= 1000000 && damage > 0)
	if(t_mode&0x40 && damage > 0)
		damage = 1;

	if(is_boss(target))
		blewcount = 0;

	if( tsd && tsd->state.no_weapon_damage)
		damage = 0;

	if(skill_num != CR_GRANDCROSS)
		damage=battle_calc_damage(src,target,damage,div_,skill_num,skill_lv,flag);

	wd.damage=damage;
	wd.damage2=0;
	wd.type=type;
	wd.div_=div_;
	wd.amotion=status_get_amotion(src);
	if(skill_num == KN_AUTOCOUNTER)
		wd.amotion >>= 1;
	wd.dmotion=status_get_dmotion(target);
	wd.blewcount=blewcount;
	wd.flag=flag;
	wd.dmg_lv=dmg_lv;
	return wd;
}
/*
 * =========================================================================
 * PC�̕���ɂ��U��
 *-------------------------------------------------------------------------
 */
static struct Damage battle_calc_pc_weapon_attack(
	struct block_list *src,struct block_list *target,int skill_num,int skill_lv,int wflag)
{
	struct map_session_data *sd=(struct map_session_data *)src,*tsd=NULL;
	struct mob_data *tmd=NULL;
	int hitrate,flee,cri = 0,atkmin,atkmax;
	int dex;
	unsigned int luk,target_count = 1;
	int no_cardfix=0;
	int def1 = status_get_def(target);
	int def2 = status_get_def2(target);
	int t_vit = status_get_vit(target);
	struct Damage wd;
	int damage,damage2,damage_rate=100,type,div_,blewcount=skill_get_blewcount(skill_num,skill_lv);
	int flag,skill,dmg_lv = 0;
	int t_mode=0,t_race=0,t_size=1,s_race=7,s_ele=0,s_size=1;
	int t_race2=0;
	struct status_change *sc_data,*t_sc_data;
	short *option, *opt1, *opt2;
	int atkmax_=0, atkmin_=0, s_ele_;	//�񓁗��p
	int watk,watk_,cardfix,t_ele;
	int da=0,i,t_class,ac_flag = 0;
	int ignore_def_flag = 0;
	int idef_flag=0,idef_flag_=0;
	int div_flag=0;	// 0: total damage is to be divided by div_
					// 1: damage is distributed,and has to be multiplied by div_ [celest]

	//return�O�̏���������̂ŏ��o�͕��̂ݕύX
	if( src == NULL || target == NULL || sd == NULL ){
		nullpo_info(NLP_MARK);
		memset(&wd,0,sizeof(wd));
		return wd;
	}


	// �A�^�b�J�[
	s_race=status_get_race(src); //�푰
	s_ele=status_get_attack_element(src); //����
	s_ele_=status_get_attack_element2(src); //���葮��
	s_size=status_get_size(src);
	sc_data=status_get_sc_data(src); //�X�e�[�^�X�ُ�
	option=status_get_option(src); //��Ƃ��y�R�Ƃ��J�[�g�Ƃ�
	opt1=status_get_opt1(src); //�Ή��A�����A�X�^���A�����A�È�
	opt2=status_get_opt2(src); //�ŁA�􂢁A���فA�ÈŁH
	t_race2=status_get_race2(target);

	if(skill_num != CR_GRANDCROSS) //�O�����h�N���X�łȂ��Ȃ�
		sd->state.attack_type = BF_WEAPON; //�U���^�C�v�͕���U��

	// �^�[�Q�b�g
	if(target->type==BL_PC) //�Ώۂ�PC�Ȃ�
		tsd=(struct map_session_data *)target; //tsd�ɑ��(tmd��NULL)
	else if(target->type==BL_MOB) //�Ώۂ�Mob�Ȃ�
		tmd=(struct mob_data *)target; //tmd�ɑ��(tsd��NULL)
	t_race=status_get_race( target ); //�Ώۂ̎푰
	t_ele=status_get_elem_type(target); //�Ώۂ̑���
	t_size=status_get_size( target ); //�Ώۂ̃T�C�Y
	t_mode=status_get_mode( target ); //�Ώۂ�Mode
	t_sc_data=status_get_sc_data( target ); //�Ώۂ̃X�e�[�^�X�ُ�

//�I�[�g�J�E���^�[������������
	if(skill_num == 0 || (target->type == BL_PC && battle_config.pc_auto_counter_type&2) ||
		(target->type == BL_MOB && battle_config.monster_auto_counter_type&2)) {
		if(skill_num != CR_GRANDCROSS && t_sc_data && t_sc_data[SC_AUTOCOUNTER].timer != -1) { //�O�����h�N���X�łȂ��A�Ώۂ��I�[�g�J�E���^�[��Ԃ̏ꍇ
			int dir = map_calc_dir(*src,target->x,target->y),t_dir = status_get_dir(target);
			int dist = distance(src->x,src->y,target->x,target->y);
			if(dist <= 0 || map_check_dir(dir,t_dir) ) { //�ΏۂƂ̋�����0�ȉ��A�܂��͑Ώۂ̐��ʁH
				memset(&wd,0,sizeof(wd));
				t_sc_data[SC_AUTOCOUNTER].val3 = 0;
				t_sc_data[SC_AUTOCOUNTER].val4 = 1;
				if(sc_data && sc_data[SC_AUTOCOUNTER].timer == -1) { //�������I�[�g�J�E���^�[���
					int range = status_get_range(target);
					if((target->type == BL_PC && ((struct map_session_data *)target)->status.weapon != 11 && dist <= range+1) || //�Ώۂ�PC�ŕ��킪�|��łȂ��˒���
						(target->type == BL_MOB && range <= 3 && dist <= range+1) ) //�܂��͑Ώۂ�Mob�Ŏ˒���3�ȉ��Ŏ˒���
						t_sc_data[SC_AUTOCOUNTER].val3 = src->id;
				}
				return wd; //�_���[�W�\���̂�Ԃ��ďI��
			}
			else ac_flag = 1;
		} else if(skill_num != CR_GRANDCROSS && t_sc_data && t_sc_data[SC_POISONREACT].timer != -1) {   // poison react [Celest]
			t_sc_data[SC_POISONREACT].val3 = 0;
			t_sc_data[SC_POISONREACT].val4 = 1;
			t_sc_data[SC_POISONREACT].val3 = src->id;
		}
	}
//�I�[�g�J�E���^�[���������܂�

	flag = BF_SHORT|BF_WEAPON|BF_NORMAL;	// �U���̎�ނ̐ݒ�

	// ��𗦌v�Z�A��𔻒�͌��
	flee = status_get_flee(target);
	if(battle_config.agi_penalty_type > 0 || battle_config.vit_penalty_type > 0) //AGI�AVIT�y�i���e�B�ݒ肪�L��
		target_count += battle_counttargeted(*target,src,battle_config.agi_penalty_count_lv);	//�Ώۂ̐����Z�o
	if(battle_config.agi_penalty_type > 0) {
		if(target_count >= battle_config.agi_penalty_count) { //�y�i���e�B�ݒ���Ώۂ�����
			if(battle_config.agi_penalty_type == 1) //��𗦂�agi_penalty_num%������
				flee = (flee * (100 - (target_count - (battle_config.agi_penalty_count - 1))*battle_config.agi_penalty_num))/100;
			else if(battle_config.agi_penalty_type == 2) //��𗦂�agi_penalty_num������
				flee -= (target_count - (battle_config.agi_penalty_count - 1))*battle_config.agi_penalty_num;
			if(flee < 1) flee = 1; //��𗦂͍Œ�ł�1
		}
	}
	hitrate = status_get_hit(src) - flee + 80; //�������v�Z

	type = 0;	// normal
	if (skill_num > 0) {
		div_=skill_get_num(skill_num,skill_lv);
		if (div_ < 1) div_ = 1;	//Avoid the rare case where the db says div_ is 0 and below
	} else div_ = 1; // single attack

	dex = status_get_dex(src); //DEX
	luk = status_get_luk(src); //LUK
	watk = status_get_atk(src); //ATK
	watk_ = status_get_atk_(src); //ATK����

	if(skill_num == HW_MAGICCRASHER)	/* �}�W�b�N�N���b�V���[��MATK�ŉ��� */
		damage = damage2 = status_get_matk1(src); //damega,damega2���o��Abase_atk�̎擾
	else
		damage = damage2 = status_get_baseatk(&sd->bl); //damega,damega2���o��Abase_atk�̎擾

	atkmin = atkmin_ = dex; //�Œ�ATK��DEX�ŏ������H
	sd->state.arrow_atk = 0; //arrow_atk������
	if(sd->equip_index[9] < MAX_INVENTORY && sd->inventory_data[sd->equip_index[9]])
		atkmin = atkmin*(80 + sd->inventory_data[sd->equip_index[9]]->wlv*20)/100;
	if(sd->equip_index[8] < MAX_INVENTORY && sd->inventory_data[sd->equip_index[8]])
		atkmin_ = atkmin_*(80 + sd->inventory_data[sd->equip_index[8]]->wlv*20)/100;
	if(sd->status.weapon == 11) { //���킪�|��̏ꍇ
		atkmin = watk * ((atkmin<watk)? atkmin:watk)/100; //�|�p�Œ�ATK�v�Z
		flag=(flag&~BF_RANGEMASK)|BF_LONG; //�������U���t���O��L��
		if(sd->arrow_ele > 0) //������Ȃ瑮�����̑����ɕύX
			s_ele = sd->arrow_ele;
		sd->state.arrow_atk = 1; //arrow_atk�L����
	}

	// �T�C�Y�C��
	// �y�R�R�悵�Ă��āA���ōU�������ꍇ�͒��^�̃T�C�Y�C����100�ɂ���
	// �E�F�|���p�[�t�F�N�V����,�h���C�NC
	if(sd->state.no_sizefix ||
		skill_num == MO_EXTREMITYFIST ||
		(sc_data && sc_data[SC_WEAPONPERFECTION].timer!=-1) ||
		(pc_isriding(*sd) && (sd->status.weapon == 4 || sd->status.weapon == 5) && t_size == 1))
	{
		atkmax = watk;
		atkmax_ = watk_;
	} else {
		atkmax = (watk * sd->right_weapon.atkmods[ t_size ]) / 100;
		atkmin = (atkmin * sd->right_weapon.atkmods[ t_size ]) / 100;
		atkmax_ = (watk_ * sd->left_weapon.atkmods[ t_size ]) / 100;
		atkmin_ = (atkmin_ * sd->left_weapon.atkmods[ t_size ]) / 100;
	}

	if (sc_data && sc_data[SC_MAXIMIZEPOWER].timer!=-1) {	// �}�L�V�}�C�Y�p���[
		atkmin = atkmax;
		atkmin_ = atkmax_;
	}
	
	if (atkmin > atkmax && !(sd->state.arrow_atk)) atkmin = atkmax;	//�|�͍ŒႪ����ꍇ����
	if (atkmin_ > atkmax_) atkmin_ = atkmax_;	

	if (skill_num == 0) {
		//�_�u���A�^�b�N����
		int da_rate = pc_checkskill(*sd,TF_DOUBLE) * 5;
		if (sd->weapontype1 == 0x01 && da_rate > 0)
			da = (rand()%100 < da_rate) ? 1 : 0;
		//�O�i��	 // triple blow works with bows ^^ [celest]
		if (sd->status.weapon <= 16 && (skill = pc_checkskill(*sd,MO_TRIPLEATTACK)) > 0)
			da = (rand()%100 < (30 - skill)) ? 2 : 0;
		if (da == 0 && sd->double_rate > 0)
			// success rate from Double Attack is counted in
			da = (rand()%100 < sd->double_rate + da_rate) ? 1 : 0;
	}

	// �ߏ萸�B�{�[�i�X
	if (sd->right_weapon.overrefine > 0)
		damage += (rand() % sd->right_weapon.overrefine) + 1;
	if (sd->left_weapon.overrefine > 0)
		damage2 += (rand() % sd->left_weapon.overrefine) + 1;

	if (da == 0) { //�_�u���A�^�b�N���������Ă��Ȃ�
		// �N���e�B�J���v�Z
		cri = status_get_critical(src) + sd->critaddrace[t_race];

		if (sd->state.arrow_atk)
			cri += sd->arrow_cri;
		if(sd->status.weapon == 16)	// �J�^�[���̏ꍇ�A�N���e�B�J����{��
			cri <<= 1;
		cri -= status_get_luk(target) * 3;

		if (t_sc_data) {
			if (t_sc_data[SC_SLEEP].timer != -1)	// �������̓N���e�B�J�����{��
				cri <<=1;
			if (t_sc_data[SC_JOINTBEAT].timer != -1 &&
				t_sc_data[SC_JOINTBEAT].val2 == 5) // Always take crits with Neck broken by Joint Beat [DracoRPG]
				cri = 1000;
		}
		if (ac_flag) cri = 1000;

		if (skill_num == KN_AUTOCOUNTER) {
			if (!(battle_config.pc_auto_counter_type&1))
				cri = 1000;
			else
				cri <<= 1;
		}
		else if (skill_num == SN_SHARPSHOOTING)
			cri += 200;
	}

	if(tsd && tsd->critical_def)
		cri = cri * (100-tsd->critical_def) / 100;

	if (da == 0 && (skill_num == 0 || skill_num == KN_AUTOCOUNTER || skill_num == SN_SHARPSHOOTING) && //�_�u���A�^�b�N���������Ă��Ȃ�
		(rand() % 1000) < cri)	// ����i�X�L���̏ꍇ�͖����j
	{
		damage += atkmax;
		damage2 += atkmax_;
		if (sd->atk_rate != 100 || sd->weapon_atk_rate != 0) {
			if (sd->status.weapon < 16) {
				damage = (damage * (sd->atk_rate + sd->weapon_atk_rate[sd->status.weapon]))/100;
				damage2 = (damage2 * (sd->atk_rate + sd->weapon_atk_rate[sd->status.weapon]))/100;
			}
		}
		if (sd->state.arrow_atk)
			damage += sd->arrow_atk;
		damage += damage * sd->crit_atk_rate / 100;
		type = 0x0a;
	} else {
		int vitbonusmax;

		if(atkmax > atkmin)
			damage += atkmin + rand() % (atkmax-atkmin + 1);
		else
			damage += atkmin ;
		if(atkmax_ > atkmin_)
			damage2 += atkmin_ + rand() % (atkmax_-atkmin_ + 1);
		else
			damage2 += atkmin_ ;
		if(sd->atk_rate != 100 || sd->weapon_atk_rate != 0) {
			if (sd->status.weapon < 16) {
				damage = (damage * (sd->atk_rate + sd->weapon_atk_rate[sd->status.weapon]))/100;
				damage2 = (damage2 * (sd->atk_rate + sd->weapon_atk_rate[sd->status.weapon]))/100;
			}
		}

		if(sd->state.arrow_atk) {
			if(sd->arrow_atk > 0)
				damage += rand()%(sd->arrow_atk+1);
			hitrate += sd->arrow_hit;
		}

		if(skill_num != MO_INVESTIGATE && def1 < 1000000) {
			if(sd->right_weapon.def_ratio_atk_ele & (1<<t_ele) || sd->right_weapon.def_ratio_atk_race & (1<<t_race)) {
				damage = (damage * (def1 + def2))/100;
				idef_flag = 1;
			}
			if(sd->left_weapon.def_ratio_atk_ele & (1<<t_ele) || sd->left_weapon.def_ratio_atk_race & (1<<t_race)) {
				damage2 = (damage2 * (def1 + def2))/100;
				idef_flag_ = 1;
			}
			if(is_boss(target)) {
				if(!idef_flag && sd->right_weapon.def_ratio_atk_race & (1<<10)) {
					damage = (damage * (def1 + def2))/100;
					idef_flag = 1;
				}
				if(!idef_flag_ && sd->left_weapon.def_ratio_atk_race & (1<<10)) {
					damage2 = (damage2 * (def1 + def2))/100;
					idef_flag_ = 1;
				}
			}
			else {
				if(!idef_flag && sd->right_weapon.def_ratio_atk_race & (1<<11)) {
					damage = (damage * (def1 + def2))/100;
					idef_flag = 1;
				}
				if(!idef_flag_ && sd->left_weapon.def_ratio_atk_race & (1<<11)) {
					damage2 = (damage2 * (def1 + def2))/100;
					idef_flag_ = 1;
				}
			}
		}

		// �X�L���C���P�i�U���͔{���n�j
		// �I�[�o�[�g���X�g(+5% �` +25%),���U���n�X�L���̏ꍇ�����ŕ␳
		// �o�b�V��,�}�O�i���u���C�N,
		// �{�[�����O�o�b�V��,�X�s�A�u�[������,�u�����f�B�b�V���X�s�A,�X�s�A�X�^�b�u,
		// ���}�[�i�C�g,�J�[�g���{�����[�V����
		// �_�u���X�g���C�t�B���O,�A���[�V�����[,�`���[�W�A���[,
		// �\�j�b�N�u���[
		if(sc_data){ //��Ԉُ풆�̃_���[�W�ǉ�
			if(sc_data[SC_OVERTHRUST].timer!=-1)	// �I�[�o�[�g���X�g
				damage_rate += 5*sc_data[SC_OVERTHRUST].val1;
			if(sc_data[SC_TRUESIGHT].timer!=-1)	// �g�D���[�T�C�g
				damage_rate += 2*sc_data[SC_TRUESIGHT].val1;
			if(sc_data[SC_BERSERK].timer!=-1)	// �o�[�T�[�N
				damage_rate += 200;
			if(sc_data[SC_MAXOVERTHRUST].timer!=-1)
				damage_rate += 20*sc_data[SC_MAXOVERTHRUST].val1;
		}

		if(skill_num>0){
			int i;
			if( (i=skill_get_pl(skill_num))>0 )
				s_ele=s_ele_=i;

			div_=skill_get_num(skill_num,skill_lv); //[Skotlex] Added number of hits calc
			flag=(flag&~BF_SKILLMASK)|BF_SKILL;
			switch( skill_num ){
			case SM_BASH:		// �o�b�V��
				damage_rate += 30*skill_lv;
				hitrate = (hitrate*(100+5*skill_lv))/100;
				break;
			case SM_MAGNUM:		// �}�O�i���u���C�N
				// 20*skill level+100? i think this will do for now [based on jRO info]
				damage_rate += (wflag > 1 ? 5*skill_lv+15 : 30*skill_lv);
				hitrate = (hitrate*(100+10*skill_lv))/100;
				break;
			case MC_MAMMONITE:	// ���}�[�i�C�g
				damage_rate += 50*skill_lv;
				break;
			case AC_DOUBLE:	// �_�u���X�g���C�t�B���O
				if(!sd->state.arrow_atk && sd->arrow_atk > 0) {
					int arr = rand()%(sd->arrow_atk+1);
					damage += arr;
					damage2 += arr;
				}
				damage_rate += 80+ 20*skill_lv;
				if(sd->arrow_ele > 0) {
					s_ele = sd->arrow_ele;
					s_ele_ = sd->arrow_ele;
				}
				flag=(flag&~BF_RANGEMASK)|BF_LONG;
				sd->state.arrow_atk = 1;
				break;
			case AC_SHOWER:	// �A���[�V�����[
				if(!sd->state.arrow_atk && sd->arrow_atk > 0) {
					int arr = rand()%(sd->arrow_atk+1);
					damage += arr;
					damage2 += arr;
				}
				damage_rate += 5*skill_lv-25;
				if(sd->arrow_ele > 0) {
					s_ele = sd->arrow_ele;
					s_ele_ = sd->arrow_ele;
				}
				flag=(flag&~BF_RANGEMASK)|BF_LONG;
				sd->state.arrow_atk = 1;
				break;
			case AC_CHARGEARROW:	// �`���[�W�A���[
				if(!sd->state.arrow_atk && sd->arrow_atk > 0) {
					int arr = rand()%(sd->arrow_atk+1);
					damage += arr;
					damage2 += arr;
				}
				damage_rate += 50;
				if(sd->arrow_ele > 0) {
					s_ele = sd->arrow_ele;
					s_ele_ = sd->arrow_ele;
				}
				flag=(flag&~BF_RANGEMASK)|BF_LONG;
				sd->state.arrow_atk = 1;
				break;
			case KN_PIERCE:	// �s�A�[�X
				damage_rate += 10*skill_lv;
				hitrate=hitrate*(100+5*skill_lv)/100;
				div_=t_size+1;
				div_flag=1;
				break;
			case KN_SPEARSTAB:	// �X�s�A�X�^�u
				damage_rate += 15*skill_lv;
				blewcount=0;
				break;
			case KN_SPEARBOOMERANG:	// �X�s�A�u�[������
				damage_rate += 50*skill_lv;
				flag=(flag&~BF_RANGEMASK)|BF_LONG;
				break;
			case KN_BRANDISHSPEAR: // �u�����f�B�b�V���X�s�A
				damage_rate += 20*skill_lv;
				if(skill_lv>3 && wflag==1) damage_rate += 100/2;
				if(skill_lv>6 && wflag==1) damage_rate += 100/4;
				if(skill_lv>9 && wflag==1) damage_rate += 100/8;
				if(skill_lv>6 && wflag==2) damage_rate += 100/2;
				if(skill_lv>9 && wflag==2) damage_rate += 100/4;
				if(skill_lv>9 && wflag==3) damage_rate += 100/2;
				break;
			case KN_BOWLINGBASH:	// �{�E�����O�o�b�V��
				damage_rate = 50*skill_lv;
				blewcount=0;
				break;
			case KN_AUTOCOUNTER:
				if(battle_config.pc_auto_counter_type&1)
					hitrate += 20;
				else
					hitrate = 1000000;
				ignore_def_flag = 1;
				flag=(flag&~BF_SKILLMASK)|BF_NORMAL;
				break;
			case AS_GRIMTOOTH:
				damage_rate += 20*skill_lv;
				break;
			case AS_POISONREACT: // celest
				s_ele = 0;
				damage_rate += 30*skill_lv;
				break;
			case AS_SONICBLOW:	// �\�j�b�N�u���E
				hitrate+=30; // hitrate +30, thanks to midas
				damage_rate += 200+ 50*skill_lv;
				break;
			case TF_SPRINKLESAND:	// ���܂�
				damage_rate += 25;
				break;
			case MC_CARTREVOLUTION:	// �J�[�g���{�����[�V����
				if(sd->cart_max_weight > 0 && sd->cart_weight > 0) {
					damage_rate += 50 + sd->cart_weight/800;	//fixed CARTREV damage [Lupus] // should be 800, not 80... weight is *10 ^_- [celest]
				}
				else {
					damage_rate += 50;
				}
				break;
			// �ȉ�MOB
			case NPC_COMBOATTACK:	// ���i�U��
				div_flag=1;
				break;
			case NPC_RANDOMATTACK:	// �����_��ATK�U��
				damage_rate += rand()%150-50;
				break;
			// �����U���i�K���j
			case NPC_WATERATTACK:
			case NPC_GROUNDATTACK:
			case NPC_FIREATTACK:
			case NPC_WINDATTACK:
			case NPC_POISONATTACK:
			case NPC_HOLYATTACK:
			case NPC_DARKNESSATTACK:
			case NPC_UNDEADATTACK:
			case NPC_TELEKINESISATTACK:
				damage_rate += 25*skill_lv;
				break;
			case NPC_GUIDEDATTACK:
				hitrate = 1000000;
				break;
			case NPC_RANGEATTACK:
				flag=(flag&~BF_RANGEMASK)|BF_LONG;
				break;
			case NPC_PIERCINGATT:
				flag=(flag&~BF_RANGEMASK)|BF_SHORT;
				break;
			case NPC_CRITICALSLASH:
				ignore_def_flag = 1;
				break;
			case RG_BACKSTAP:	// �o�b�N�X�^�u
				if(battle_config.backstab_bow_penalty == 1 && sd->status.weapon == 11){
					damage_rate += 50+ 20*skill_lv; // Back Stab with a bow does twice less damage
				}else{
					damage_rate += 200+ 40*skill_lv;
				}
				hitrate = 1000000;
				break;
			case RG_RAID:	// �T�v���C�Y�A�^�b�N
				damage_rate += 40*skill_lv;
				break;
			case RG_INTIMIDATE:	// �C���e�B�~�f�C�g
				damage_rate += 30*skill_lv;
				break;
			case CR_SHIELDCHARGE:	// �V�[���h�`���[�W
				damage_rate += 20*skill_lv;
				flag=(flag&~BF_RANGEMASK)|BF_SHORT;
				s_ele = 0;
				break;
			case CR_SHIELDBOOMERANG:	// �V�[���h�u�[������
				damage_rate += 30*skill_lv;
				flag=(flag&~BF_RANGEMASK)|BF_LONG;
				s_ele = 0;
				break;
			case CR_HOLYCROSS:	// �z�[���[�N���X
				damage_rate += 35*skill_lv;
				break;
			case CR_GRANDCROSS:
				hitrate= 1000000;
				if(!battle_config.gx_cardfix)
					no_cardfix = 1;
				break;
			case AM_DEMONSTRATION:	// �f�����X�g���[�V����
				damage_rate += 20*skill_lv;
				no_cardfix = 1;
				break;
			case AM_ACIDTERROR:	// �A�V�b�h�e���[
				hitrate = 1000000;
				damage_rate += 40*skill_lv;
				s_ele = 0;
				s_ele_ = 0;
				ignore_def_flag = 1;
				no_cardfix = 1;
				break;
			case MO_FINGEROFFENSIVE:	//�w�e
				damage_rate += 25 + 25 * skill_lv;
				if(battle_config.finger_offensive_type == 0) {
					div_ = sd->spiritball_old;
					div_flag = 1;
				}
				else div_ = 1;
				flag=(flag&~BF_RANGEMASK)|BF_LONG;   //orn
				break;
			case MO_INVESTIGATE:	// �� ��
				if(def1 < 1000000) {
					damage = damage*(def1 + def2)/50;
					damage2 = damage2*(def1 + def2)/50;
					damage_rate += 75*skill_lv;
				}
				hitrate = 1000000;
				ignore_def_flag = 1;
				s_ele = 0;
				s_ele_ = 0;
				break;
			case MO_EXTREMITYFIST:	// ���C���e�P��
				damage = damage * (8 + (sd->status.sp/10)) + 250 + (skill_lv * 150);
				sd->status.sp = 0;
				clif_updatestatus(*sd,SP_SP);
				hitrate = 1000000;
				ignore_def_flag = 1;
				s_ele = 0;
				s_ele_ = 0;
				break;
			case MO_CHAINCOMBO:	// �A�ŏ�
				damage_rate += 50+ 50*skill_lv;
				break;
			case MO_COMBOFINISH:	// �җ���
				damage_rate += 140+ 60*skill_lv;;
				break;
			case BA_MUSICALSTRIKE:	// �~���[�W�J���X�g���C�N
				damage_rate += 40*skill_lv-40;
				if(sd->arrow_ele > 0) {
					s_ele = sd->arrow_ele;
					s_ele_ = sd->arrow_ele;
				}
				flag=(flag&~BF_RANGEMASK)|BF_LONG;
				break;
			case DC_THROWARROW:	// ���
				if(!sd->state.arrow_atk && sd->arrow_atk > 0) {
					int arr = rand()%(sd->arrow_atk+1);
					damage += arr;
					damage2 += arr;
				}
				damage_rate += 50 * skill_lv;
				if(sd->arrow_ele > 0) {
					s_ele = sd->arrow_ele;
					s_ele_ = sd->arrow_ele;
				}
				flag=(flag&~BF_RANGEMASK)|BF_LONG;
				sd->state.arrow_atk = 1;
				break;
			case CH_TIGERFIST:	// ���Ռ�
				damage_rate +=  100*skill_lv-60;
				break;
			case CH_CHAINCRUSH:	// �A������
				damage_rate += 300+ 100*skill_lv;
				break;
			case CH_PALMSTRIKE:	// �ҌՍd�h�R
				damage_rate += 100+ 100*skill_lv;
				break;
			case LK_SPIRALPIERCE:			/* �X�p�C�����s�A�[�X */
				damage_rate += 50*skill_lv; //�����ʂ�������Ȃ��̂œK����
				ignore_def_flag = 1;
				flag=(flag&~BF_RANGEMASK)|BF_LONG;
				if(tsd)
					tsd->canmove_tick = gettick() + 1000;
				else if(tmd)
					tmd->canmove_tick = gettick() + 1000;
				break;
			case LK_HEADCRUSH:				/* �w�b�h�N���b�V�� */
				damage_rate += 40*skill_lv;
				break;
			case LK_JOINTBEAT:				/* �W���C���g�r�[�g */
				damage_rate += 10*skill_lv-50;
				break;
			case ASC_METEORASSAULT:			/* ���e�I�A�T���g */
				damage_rate += 40*skill_lv-60;
				no_cardfix = 1;
				break;
			case SN_SHARPSHOOTING:			/* �V���[�v�V���[�e�B���O */
				damage_rate += 100+50*skill_lv;
				break;
			case CG_ARROWVULCAN:			/* �A���[�o���J�� */
				damage_rate += 100+100*skill_lv;
				if(sd->arrow_ele > 0) {
					s_ele = sd->arrow_ele;
					s_ele_ = sd->arrow_ele;
				}
				flag=(flag&~BF_RANGEMASK)|BF_LONG;
				break;
			case AS_SPLASHER:		/* �x�i���X�v���b�V���[ */
				damage_rate += 100+20*skill_lv+20*pc_checkskill(*sd,AS_POISONREACT);
				no_cardfix = 1;
				hitrate = 1000000;
				break;
			case ASC_BREAKER:
				// calculate physical part of damage
				damage_rate += 100*skill_lv-100;
				flag=(flag&~BF_RANGEMASK)|BF_LONG;
				break;
			case PA_SHIELDCHAIN:
				damage_rate += 30*skill_lv-100;
				flag = (flag&~BF_RANGEMASK)|BF_LONG;
				hitrate += 20;
				div_flag = 1;
				s_ele = 0;
				break;
			case WS_CARTTERMINATION:
				if(sd->cart_max_weight > 0 && sd->cart_weight > 0) {
					damage_rate += sd->cart_weight/(10*(16-skill_lv))-100;
				}
				break;
			case CR_ACIDDEMONSTRATION:
				div_flag = 1;
				// damage according to vit and int
				break;
			}

			//what about normal attacks? [celest]
			//damage *= damage_rate/100;
			//damage2 *= damage_rate/100;

			if (div_flag && div_ > 1) {	// [Skotlex]
				damage *= div_;
				damage2 *= div_;
			}
			if (sd && skill_num > 0 && sd->skillatk[0] == skill_num)
				damage += damage*sd->skillatk[1]/100;
		}

		damage = damage * damage_rate / 100;
		damage2 = damage2 * damage_rate / 100;

		if(da == 2) { //�O�i�����������Ă��邩
			type = 0x08;
			div_ = 255;	//�O�i���p�Ɂc
			damage = damage * (100 + 20 * pc_checkskill(*sd, MO_TRIPLEATTACK)) / 100;
		}

		// �� �ۂ̖h��͂ɂ��_���[�W�̌���
		// �f�B�o�C���v���e�N�V�����i�����ł����̂��ȁH�j
		if (!ignore_def_flag && def1 < 1000000) {	//DEF, VIT����
			int t_def;
			target_count = 1 + battle_counttargeted(*target,src,battle_config.vit_penalty_count_lv);
			if(battle_config.vit_penalty_type > 0) {
				if(target_count >= battle_config.vit_penalty_count) {
					if(battle_config.vit_penalty_type == 1) {
						def1 = (def1 * (100 - (target_count - (battle_config.vit_penalty_count - 1))*battle_config.vit_penalty_num))/100;
						def2 = (def2 * (100 - (target_count - (battle_config.vit_penalty_count - 1))*battle_config.vit_penalty_num))/100;
						t_vit = (t_vit * (100 - (target_count - (battle_config.vit_penalty_count - 1))*battle_config.vit_penalty_num))/100;
					}
					else if(battle_config.vit_penalty_type == 2) {
						def1 -= (target_count - (battle_config.vit_penalty_count - 1))*battle_config.vit_penalty_num;
						def2 -= (target_count - (battle_config.vit_penalty_count - 1))*battle_config.vit_penalty_num;
						t_vit -= (target_count - (battle_config.vit_penalty_count - 1))*battle_config.vit_penalty_num;
					}
					if(def1 < 0) def1 = 0;
					if(def2 < 1) def2 = 1;
					if(t_vit < 1) t_vit = 1;
				}
			}
			t_def = def2*8/10;
			vitbonusmax = (t_vit/20)*(t_vit/20)-1;
			if (tmd) {
				if(is_boss(target)) {
					if(sd->right_weapon.ignore_def_mob & 2)
						idef_flag = 1;
					if(sd->left_weapon.ignore_def_mob & 2)
						idef_flag_ = 1;
				} else {
					if(sd->right_weapon.ignore_def_mob & 1)
						idef_flag = 1;
					if(sd->left_weapon.ignore_def_mob & 1)
						idef_flag_ = 1;
				}
			}
			if(sd->right_weapon.ignore_def_ele & (1<<t_ele) || sd->right_weapon.ignore_def_race & (1<<t_race))
				idef_flag = 1;
			if(sd->left_weapon.ignore_def_ele & (1<<t_ele) || sd->left_weapon.ignore_def_race & (1<<t_race))
				idef_flag_ = 1;
			if(is_boss(target)) {
				if(sd->right_weapon.ignore_def_race & (1<<10))
					idef_flag = 1;
				if(sd->left_weapon.ignore_def_race & (1<<10))
					idef_flag_ = 1;
			}
			else {
				if(sd->right_weapon.ignore_def_race & (1<<11))
					idef_flag = 1;
				if(sd->left_weapon.ignore_def_race & (1<<11))
					idef_flag_ = 1;
			}

			if(!idef_flag){
				if(battle_config.player_defense_type) {
					damage = damage - (def1 * battle_config.player_defense_type) - t_def - ((vitbonusmax < 1)?0: rand()%(vitbonusmax+1) );
				}
				else{
					damage = damage * (100 - def1) /100 - t_def - ((vitbonusmax < 1)?0: rand()%(vitbonusmax+1) );
				}
			}
			if(!idef_flag_){
				if(battle_config.player_defense_type) {
					damage2 = damage2 - (def1 * battle_config.player_defense_type) - t_def - ((vitbonusmax < 1)?0: rand()%(vitbonusmax+1) );
				}
				else{
					damage2 = damage2 * (100 - def1) /100 - t_def - ((vitbonusmax < 1)?0: rand()%(vitbonusmax+1) );
				}
			}
		}
	}

	// ��Ԉُ풆�̃_���[�W�ǉ��ŃN���e�B�J���ɂ��L���ȃX�L��
	if (sc_data) {
		// �G���`�����g�f�b�h���[�|�C�Y��
		if(!no_cardfix && sc_data[SC_EDP].timer != -1 &&
			skill_num != ASC_BREAKER && skill_num != ASC_METEORASSAULT)
		{
			damage += damage * (150 + sc_data[SC_EDP].val1 * 50) / 100;
			no_cardfix = 1;
		}
		// sacrifice works on boss monsters, and does 9% damage to self [Celest]
		if (!skill_num && sc_data[SC_SACRIFICE].timer != -1) {
			int dmg = status_get_max_hp(src) * 9/100;
			pc_heal(*sd, -dmg, 0);
			damage = dmg * (90 + sc_data[SC_SACRIFICE].val1 * 10) / 100;
			damage2 = 0;
			hitrate = 1000000;
			s_ele = 0;
			s_ele_ = 0;
			skill_num = PA_SACRIFICE;
			//clif_skill_nodamage(*src,*target,skill_num,skill_lv,1);	// this doesn't show effect either.. hmm =/
			sc_data[SC_SACRIFICE].val2 --;
			if (sc_data[SC_SACRIFICE].val2 == 0)
				status_change_end(src, SC_SACRIFICE,-1);
		}
	}

	// ���B�_���[�W�̒ǉ�
	if( skill_num != MO_INVESTIGATE && skill_num != MO_EXTREMITYFIST) {			//DEF, VIT����
		damage += status_get_atk2(src);
		damage2 += status_get_atk_2(src);
	}
	if(skill_num == CR_SHIELDBOOMERANG || skill_num == PA_SHIELDCHAIN) {
		if(sd->equip_index[8] < MAX_INVENTORY) {
			int index = sd->equip_index[8];
			if(sd->inventory_data[index] && sd->inventory_data[index]->type == 5) {
				damage += sd->inventory_data[index]->weight/10;
				damage += sd->status.inventory[index].refine * status_getrefinebonus(0,1);
			}
		}
	}
	else if(skill_num == LK_SPIRALPIERCE) {			/* �X�p�C�����s�A�[�X */
		if(sd->equip_index[9] < MAX_INVENTORY) {	//�d�ʂŒǉ��_���[�W�炵���̂ŃV�[���h�u�[���������Q�l�ɒǉ�
			int index = sd->equip_index[9];
			if(sd->inventory_data[index] && sd->inventory_data[index]->type == 4) {
				damage += (sd->inventory_data[index]->weight*(skill_lv*4*4/10/5));
				damage += sd->status.inventory[index].refine * status_getrefinebonus(0,1);
			}
		}
	}

	// 0�����������ꍇ1�ɕ␳
	if(damage<1) damage=1;
	if(damage2<1) damage2=1;

	// �X�L���C���Q�i�C���n�j
	// �C���_���[�W(�E��̂�) �\�j�b�N�u���[���͕ʏ����i1���ɕt��1/8�K��)
	if( skill_num != MO_INVESTIGATE && skill_num != MO_EXTREMITYFIST && skill_num != CR_GRANDCROSS) {			//�C���_���[�W����
		damage = battle_addmastery(sd,target,damage,0);
		damage2 = battle_addmastery(sd,target,damage2,1);
	}

	// Aura Blade adds dmg [DracoRPG]
	if(sc_data && sc_data[SC_AURABLADE].timer!=-1) {
		damage += sc_data[SC_AURABLADE].val1 * 20;
		damage2 += sc_data[SC_AURABLADE].val1 * 20;
	}

	// A weapon forged by a Blacksmith in the Fame Top 10 gets +10 dmg [DracoRPG]
	if(sd->right_weapon.fameflag)
		damage += 10;
	if(sd->left_weapon.fameflag)
	    damage2 += 10;

	if (sd->perfect_hit > 0 && rand()%100 < sd->perfect_hit)
		hitrate = 1000000;

	// ����C��
	if (hitrate < 1000000 && t_sc_data) {			// �K���U��
		if (t_sc_data[SC_FOGWALL].timer != -1 && flag&BF_LONG)
			hitrate -= 75;
		if (t_sc_data[SC_SLEEP].timer!=-1 ||	// �����͕K��
			t_sc_data[SC_STAN].timer!=-1 ||		// �X�^���͕K��
			t_sc_data[SC_FREEZE].timer!=-1 ||
			(t_sc_data[SC_STONE].timer!=-1 && t_sc_data[SC_STONE].val2==0))	// �����͕K��
			hitrate = 1000000;
	}
	// weapon research hidden bonus
	if ((skill = pc_checkskill(*sd,BS_WEAPONRESEARCH)) > 0) {
		hitrate = hitrate * (100+2*skill) / 100;
	}
	if(hitrate < 5)
		hitrate = 5;
	if (type == 0 && rand()%100 >= hitrate) {
		damage = damage2 = 0;
		dmg_lv = ATK_FLEE;
	} else {
		dmg_lv = ATK_DEF;
	}

	// �X�L���C���R�i���팤���j
	if ((skill = pc_checkskill(*sd,BS_WEAPONRESEARCH)) > 0) {
		damage += skill*2;
		damage2 += skill*2;
	}

//�X�L���ɂ��_���[�W�␳�����܂�

//�J�[�h�ɂ��_���[�W�ǉ�������������
	cardfix=100;
	if(!sd->state.arrow_atk) { //�|��ȊO
		if(!battle_config.left_cardfix_to_right) { //����J�[�h�␳�ݒ薳��
			cardfix=cardfix*(100+sd->right_weapon.addrace[t_race])/100;	// �푰�ɂ��_���[�W�C��
			cardfix=cardfix*(100+sd->right_weapon.addele[t_ele])/100;	// �����ɂ��_���[�W�C��
			cardfix=cardfix*(100+sd->right_weapon.addsize[t_size])/100;	// �T�C�Y�ɂ��_���[�W�C��
			cardfix=cardfix*(100+sd->right_weapon.addrace2[t_race2])/100;
		}
		else {
			cardfix=cardfix*(100+sd->right_weapon.addrace[t_race]+sd->left_weapon.addrace[t_race])/100;	// �푰�ɂ��_���[�W�C��(����ɂ��ǉ�����)
			cardfix=cardfix*(100+sd->right_weapon.addele[t_ele]+sd->left_weapon.addele[t_ele])/100;	// �����ɂ��_���[�W�C��(����ɂ��ǉ�����)
			cardfix=cardfix*(100+sd->right_weapon.addsize[t_size]+sd->left_weapon.addsize[t_size])/100;	// �T�C�Y�ɂ��_���[�W�C��(����ɂ��ǉ�����)
			cardfix=cardfix*(100+sd->right_weapon.addrace2[t_race2]+sd->left_weapon.addrace2[t_race2])/100;
		}
	}
	else { //�|��
		cardfix=cardfix*(100+sd->right_weapon.addrace[t_race]+sd->arrow_addrace[t_race])/100;	// �푰�ɂ��_���[�W�C��(�|��ɂ��ǉ�����)
		cardfix=cardfix*(100+sd->right_weapon.addele[t_ele]+sd->arrow_addele[t_ele])/100;	// �����ɂ��_���[�W�C��(�|��ɂ��ǉ�����)
		cardfix=cardfix*(100+sd->right_weapon.addsize[t_size]+sd->arrow_addsize[t_size])/100;	// �T�C�Y�ɂ��_���[�W�C��(�|��ɂ��ǉ�����)
		cardfix=cardfix*(100+sd->right_weapon.addrace2[t_race2])/100;
	}
	if(is_boss(target)) { //�{�X
		if(!sd->state.arrow_atk) { //�|��U���ȊO�Ȃ�
			if(!battle_config.left_cardfix_to_right) //����J�[�h�␳�ݒ薳��
				cardfix=cardfix*(100+sd->right_weapon.addrace[10])/100; //�{�X�����X�^�[�ɒǉ��_���[�W
			else //����J�[�h�␳�ݒ肠��
				cardfix=cardfix*(100+sd->right_weapon.addrace[10]+sd->left_weapon.addrace[10])/100; //�{�X�����X�^�[�ɒǉ��_���[�W(����ɂ��ǉ�����)
		}
		else //�|��U��
			cardfix=cardfix*(100+sd->right_weapon.addrace[10]+sd->arrow_addrace[10])/100; //�{�X�����X�^�[�ɒǉ��_���[�W(�|��ɂ��ǉ�����)
	}
	else { //�{�X����Ȃ�
		if(!sd->state.arrow_atk) { //�|��U���ȊO
			if(!battle_config.left_cardfix_to_right) //����J�[�h�␳�ݒ薳��
				cardfix=cardfix*(100+sd->right_weapon.addrace[11])/100; //�{�X�ȊO�����X�^�[�ɒǉ��_���[�W
			else //����J�[�h�␳�ݒ肠��
				cardfix=cardfix*(100+sd->right_weapon.addrace[11]+sd->left_weapon.addrace[11])/100; //�{�X�ȊO�����X�^�[�ɒǉ��_���[�W(����ɂ��ǉ�����)
		}
		else
			cardfix=cardfix*(100+sd->right_weapon.addrace[11]+sd->arrow_addrace[11])/100; //�{�X�ȊO�����X�^�[�ɒǉ��_���[�W(�|��ɂ��ǉ�����)
	}
	//����Class�p�␳����(�����̓��L���{���S���p�H)
	t_class = status_get_class(target);
	for(i=0;i<sd->right_weapon.add_damage_class_count;i++) {
		if(sd->right_weapon.add_damage_classid[i] == t_class) {
			cardfix=cardfix*(100+sd->right_weapon.add_damage_classrate[i])/100;
			break;
		}
	}
	if(!no_cardfix)
		damage=damage*cardfix/100; //�J�[�h�␳�ɂ��_���[�W����
//�J�[�h�ɂ��_���[�W�������������܂�

//�J�[�h�ɂ��_���[�W�ǉ�����(����)��������
	cardfix=100;
	if(!battle_config.left_cardfix_to_right) {  //����J�[�h�␳�ݒ薳��
		cardfix=cardfix*(100+sd->left_weapon.addrace[t_race])/100;	// �푰�ɂ��_���[�W�C������
		cardfix=cardfix*(100+sd->left_weapon.addele[t_ele])/100;	// �� ���ɂ��_���[�W�C������
		cardfix=cardfix*(100+sd->left_weapon.addsize[t_size])/100;	// �T�C�Y�ɂ��_���[�W�C������
		cardfix=cardfix*(100+sd->left_weapon.addrace2[t_race2])/100;
		if(is_boss(target)) //�{�X
			cardfix=cardfix*(100+sd->left_weapon.addrace[10])/100; //�{�X�����X�^�[�ɒǉ��_���[�W����
		else
			cardfix=cardfix*(100+sd->left_weapon.addrace[11])/100; //�{�X�ȊO�����X�^�[�ɒǉ��_���[�W����
	}
	//����Class�p�␳��������(�����̓��L���{���S���p�H)
	for(i=0;i<sd->left_weapon.add_damage_class_count;i++) {
		if(sd->left_weapon.add_damage_classid[i] == t_class) {
			cardfix=cardfix*(100+sd->left_weapon.add_damage_classrate[i])/100;
			break;
		}
	}
	if(!no_cardfix)
		damage2=damage2*cardfix/100;

//�J�[�h�␳�ɂ�鍶��_���[�W����
//�J�[�h�ɂ��_���[�W��������(����)�����܂�

//�J�[�h�ɂ��_���[�W����������������
	if(tsd){ //�Ώۂ�PC�̏ꍇ
		cardfix=100;
		cardfix=cardfix*(100-tsd->subrace[s_race])/100;	// �푰�ɂ��_���[�W�ϐ�
		cardfix=cardfix*(100-tsd->subele[s_ele])/100;	// �����ɂ��_���[�W�ϐ�
		cardfix=cardfix*(100-tsd->subsize[s_size])/100;
		if(is_boss(src))
			cardfix=cardfix*(100-tsd->subrace[10])/100; //�{�X����̍U���̓_���[�W����
		else
			cardfix=cardfix*(100-tsd->subrace[11])/100; //�{�X�ȊO����̍U���̓_���[�W����
		//����Class�p�␳��������(�����̓��L���{���S���p�H)
		for(i=0;i<tsd->add_def_class_count;i++) {
			if(tsd->add_def_classid[i] == sd->status.class_) {
				cardfix=cardfix*(100-tsd->add_def_classrate[i])/100;
				break;
			}
		}
		if(flag&BF_LONG)
			cardfix=cardfix*(100-tsd->long_attack_def_rate)/100; //�������U���̓_���[�W����(�z����C�Ƃ�)
		if(flag&BF_SHORT)
			cardfix=cardfix*(100-tsd->near_attack_def_rate)/100; //�ߋ����U���̓_���[�W����(�Y�������H)
		damage=damage*cardfix/100; //�J�[�h�␳�ɂ��_���[�W����
		damage2=damage2*cardfix/100; //�J�[�h�␳�ɂ�鍶��_���[�W����
	}
//�J�[�h�ɂ��_���[�W�������������܂�

//�ΏۂɃX�e�[�^�X�ُ킪����ꍇ�̃_���[�W���Z������������
	if(t_sc_data) {
		cardfix=100;
		if(t_sc_data[SC_DEFENDER].timer != -1 && flag&BF_LONG) //�f�B�t�F���_�[��Ԃŉ������U��
			cardfix=cardfix*(100-t_sc_data[SC_DEFENDER].val2)/100; //�f�B�t�F���_�[�ɂ�錸��
		if(t_sc_data[SC_FOGWALL].timer != -1 && flag&BF_LONG)
			cardfix=cardfix*50/100;
		if(cardfix != 100) {
			damage=damage*cardfix/100; //�f�B�t�F���_�[�␳�ɂ��_���[�W����
			damage2=damage2*cardfix/100; //�f�B�t�F���_�[�␳�ɂ�鍶��_���[�W����
		}
		if(t_sc_data[SC_ASSUMPTIO].timer != -1){ //�A�X���v�e�B�I
			if(!map[target->m].flag.pvp){
				damage=damage/3;
				damage2=damage2/3;
			}else{
				damage=damage/2;
				damage2=damage2/2;
			}
		}
	}
//�ΏۂɃX�e�[�^�X�ُ킪����ꍇ�̃_���[�W���Z���������܂�

	if(damage < 0) damage = 0;
	if(damage2 < 0) damage2 = 0;

	// �� ���̓K�p
	damage=battle_attr_fix(damage,s_ele, status_get_element(target) );
	damage2=battle_attr_fix(damage2,s_ele_, status_get_element(target) );

	// ���̂�����A�C���̓K�p
	damage += sd->right_weapon.star;
	damage2 += sd->left_weapon.star;
	damage += sd->spiritball*3;
	damage2 += sd->spiritball*3;

	if(skill_num==PA_PRESSURE){ /* �v���b�V���[ �K��? */
		damage = 500+300*skill_lv;
		damage2 = 500+300*skill_lv;
	}

	// >�񓁗��̍��E�_���[�W�v�Z�N������Ă��ꂥ�������������I
	// >map_session_data �ɍ���_���[�W(atk,atk2)�ǉ�����
	// >status_calc_pc()�ł��ׂ����ȁH
	// map_session_data �ɍ��蕐��(atk,atk2,ele,star,atkmods)�ǉ�����
	// status_calc_pc()�Ńf�[�^����͂��Ă��܂�

	//����̂ݕ��푕��
	if(sd->weapontype1 == 0 && sd->weapontype2 > 0) {
		damage = damage2;
		damage2 = 0;
	}

	// �E��A����C���̓K�p
	if(sd->status.weapon > 16) {// �񓁗���?
		int dmg = damage, dmg2 = damage2;
		// �E��C��(60% �` 100%) �E��S��
		skill = pc_checkskill(*sd,AS_RIGHT);
		damage = damage * (50 + (skill * 10))/100;
		if(dmg > 0 && damage < 1) damage = 1;
		// ����C��(40% �` 80%) ����S��
		skill = pc_checkskill(*sd,AS_LEFT);
		damage2 = damage2 * (30 + (skill * 10))/100;
		if(dmg2 > 0 && damage2 < 1) damage2 = 1;
	}
	else //�񓁗��łȂ���΍���_���[�W��0
		damage2 = 0;

	// �E��,�Z���̂�
	if(da == 1) { //�_�u���A�^�b�N���������Ă��邩
		div_ = 2;
		damage += damage;
		type = 0x08;
	}

	if(sd->status.weapon == 16) {
		// �J�^�[���ǌ��_���[�W
		skill = pc_checkskill(*sd,TF_DOUBLE);
		damage2 = damage * (1 + (skill * 2))/100;
		if(damage > 0 && damage2 < 1) damage2 = 1;
	}

	// �C���x�i���C��
	if(skill_num==TF_POISON){
		damage = battle_attr_fix(damage + 15*skill_lv, s_ele, status_get_element(target) );
	}
	if(skill_num==MC_CARTREVOLUTION){
		damage = battle_attr_fix(damage, 0, status_get_element(target) );
	}

	// ���S����̔���
	if(skill_num == 0 && tsd!=NULL && div_ < 255 && rand()%1000 < status_get_flee2(target) ){
		damage=damage2=0;
		type=0x0b;
		dmg_lv = ATK_LUCKY;
	}

	// �Ώۂ����S���������ݒ肪ON�Ȃ�
	if(battle_config.enemy_perfect_flee) {
		if(skill_num == 0 && tmd!=NULL && div_ < 255 && rand()%1000 < status_get_flee2(target) ) {
			damage=damage2=0;
			type=0x0b;
			dmg_lv = ATK_LUCKY;
		}
	}

	//Mob��Mode�Ɋ拭�t���O�������Ă���Ƃ��̏���
	if(t_mode&0x40){
		if(damage > 0)
			damage = 1;
		if(damage2 > 0)
			damage2 = 1;
	}

	if(is_boss(target))
		blewcount = 0;

	//bNoWeaponDamage(�ݒ�A�C�e�������H)�ŃO�����h�N���X����Ȃ��ꍇ�̓_���[�W��0
	if( tsd && tsd->state.no_weapon_damage && skill_num != CR_GRANDCROSS)
		damage = damage2 = 0;

	if(skill_num != CR_GRANDCROSS && (damage > 0 || damage2 > 0) ) {
		if(damage2<1)		// �_���[�W�ŏI�C��
			damage=battle_calc_damage(src,target,damage,div_,skill_num,skill_lv,flag);
		else if(damage<1)	// �E�肪�~�X�H
			damage2=battle_calc_damage(src,target,damage2,div_,skill_num,skill_lv,flag);
		else {	// �� ��/�J�^�[���̏ꍇ�͂�����ƌv�Z��₱����
			int d1=damage+damage2,d2=damage2;
			damage=battle_calc_damage(src,target,damage+damage2,div_,skill_num,skill_lv,flag);
			damage2=(d2*100/d1)*damage/100;
			if(damage > 1 && damage2 < 1) damage2=1;
			damage-=damage2;
		}
	}

	/*				For executioner card [Valaris]				*/
		if(src->type == BL_PC && sd->random_attack_increase_add > 0 && sd->random_attack_increase_per > 0 && skill_num == 0 ){
			if(rand()%100 < sd->random_attack_increase_per){
				if(damage >0) damage*=sd->random_attack_increase_add/100;
				if(damage2 >0) damage2*=sd->random_attack_increase_add/100;
				}
		}
	/*					End addition					*/

		// for azoth weapon [Valaris]
		if(src->type == BL_PC && target->type == BL_MOB && sd->classchange) {
			 if(rand()%10000 < sd->classchange) {
 			 	static int changeclass[]={
					1001,1002,1004,1005,1007,1008,1009,1010,1011,1012,1013,1014,1015,1016,1018,1019,1020,
					1021,1023,1024,1025,1026,1028,1029,1030,1031,1032,1033,1034,1035,1036,1037,1040,1041,
					1042,1044,1045,1047,1048,1049,1050,1051,1052,1053,1054,1055,1056,1057,1058,1060,1061,
					1062,1063,1064,1065,1066,1067,1068,1069,1070,1071,1076,1077,1078,1079,1080,1081,1083,
					1084,1085,1094,1095,1097,1099,1100,1101,1102,1103,1104,1105,1106,1107,1108,1109,1110,
					1111,1113,1114,1116,1117,1118,1119,1121,1122,1123,1124,1125,1126,1127,1128,1129,1130,
					1131,1132,1133,1134,1135,1138,1139,1140,1141,1142,1143,1144,1145,1146,1148,1149,1151,
					1152,1153,1154,1155,1156,1158,1160,1161,1163,1164,1165,1166,1167,1169,1170,1174,1175,
					1176,1177,1178,1179,1180,1182,1183,1184,1185,1188,1189,1191,1192,1193,1194,1195,1196,
					1197,1199,1200,1201,1202,1204,1205,1206,1207,1208,1209,1211,1212,1213,1214,1215,1216,
					1219,1242,1243,1245,1246,1247,1248,1249,1250,1253,1254,1255,1256,1257,1258,1260,1261,
					1263,1264,1265,1266,1267,1269,1270,1271,1273,1274,1275,1276,1277,1278,1280,1281,1282,
					1291,1292,1293,1294,1295,1297,1298,1300,1301,1302,1304,1305,1306,1308,1309,1310,1311,
					1313,1314,1315,1316,1317,1318,1319,1320,1321,1322,1323,1364,1365,1366,1367,1368,1369,
					1370,1371,1372,1374,1375,1376,1377,1378,1379,1380,1381,1382,1383,1384,1385,1386,1387,
					1390,1391,1392,1400,1401,1402,1403,1404,1405,1406,1408,1409,1410,1412,1413,1415,1416,
					1417,1493,1494,1495,1497,1498,1499,1500,1502,1503,1504,1505,1506,1507,1508,1509,1510,
					1511,1512,1513,1514,1515,1516,1517,1519,1520,1582,1584,1585,1586,1587 };
				mob_class_change( *((struct mob_data *)target),changeclass, sizeof(changeclass)/sizeof(changeclass[0]));
			}
		}

	wd.damage=damage;
	wd.damage2=damage2;
	wd.type=type;
	wd.div_=div_;
	wd.amotion=status_get_amotion(src);
	if(skill_num == KN_AUTOCOUNTER)
		wd.amotion >>= 1;
	wd.dmotion=status_get_dmotion(target);
	wd.blewcount=blewcount;
	wd.flag=flag;
	wd.dmg_lv=dmg_lv;

	return wd;
}

/*==========================================
 * battle_calc_weapon_attack_sub (by Skotlex)
 *------------------------------------------
 */
struct Damage battle_calc_weapon_attack_sub(struct block_list *src,struct block_list *target,int skill_num,int skill_lv,int wflag)
{
	struct map_session_data *sd=NULL, *tsd=NULL;
	struct mob_data *md=NULL, *tmd=NULL;
	struct pet_data *pd=NULL;//, *tpd=NULL; (Noone can target pets)
	struct Damage wd;
	short skill=0;
	unsigned short skillratio = 100;	//Skill dmg modifiers

	short i;
	short t_mode = status_get_mode(target), t_size = status_get_size(target);
	short t_race=0, t_ele=0, s_race=0;	//Set to 0 because the compiler does not notices they are NOT gonna be used uninitialized
	short s_ele, s_ele_;
	short def1, def2;
	struct status_change *sc_data = status_get_sc_data(src);
	struct status_change *t_sc_data = status_get_sc_data(target);
	struct bcwasf	{	//bcwasf stands for battle_calc_weapon_attack_sub flags
		unsigned hit : 1; //the attack Hit? (not a miss)
		unsigned cri : 1;		//Critical hit
		unsigned idef : 1;	//Ignore defense
		unsigned idef2 : 1;	//Ignore defense (left weapon)
		unsigned infdef : 1;	//Infinite defense (plants?)
		unsigned arrow : 1;	//Attack is arrow-based
		unsigned rh : 1;		//Attack considers right hand (wd.damage)
		unsigned lh : 1;		//Attack considers left hand (wd.damage2)
		unsigned cardfix : 1;
	}	flag;	

	memset(&wd,0,sizeof(wd));
	memset(&flag,0,sizeof(struct bcwasf));

	if(src==NULL || target==NULL)
	{
		nullpo_info(NLP_MARK);
		return wd;
	}
	//Initial flag
	flag.rh=1;
	flag.cardfix=1;

	//Initial Values
	wd.type=0; //Normal attack
	wd.div_=skill_get_num(skill_num,skill_lv);
	wd.amotion=status_get_amotion(src);
	if(skill_num == KN_AUTOCOUNTER)
		wd.amotion >>= 1;
	wd.dmotion=status_get_dmotion(target);
	wd.blewcount=skill_get_blewcount(skill_num,skill_lv);
	wd.flag=BF_SHORT|BF_WEAPON|BF_NORMAL; //Initial Flag
	wd.dmg_lv=ATK_DEF;	//This assumption simplifies the assignation later

	switch (src->type)
	{
		case BL_PC:
			sd=(struct map_session_data *)src;
			break;
		case BL_MOB:
			md=(struct mob_data *)src;
			break;
		case BL_PET:
			pd=(struct pet_data *)src;
			break;
	}
	switch (target->type)
	{
		case BL_PC:	
			tsd=(struct map_session_data *)target;
			if (pd) { //Pets can't target players
				memset(&wd,0,sizeof(wd));	
				return wd;
			}
			break;
		case BL_MOB:
			tmd=(struct mob_data *)target;
			break;
		case BL_PET://Cannot target pets
			memset(&wd,0,sizeof(wd));	
			return wd;
	}

	if(sd && skill_num != CR_GRANDCROSS)
		sd->state.attack_type = BF_WEAPON;

	//Set miscelanous data that needs be filled regardless of hit/miss
	if(sd)
	{
		if (sd->status.weapon == 11)
		{
			wd.flag=(wd.flag&~BF_RANGEMASK)|BF_LONG;
			flag.arrow = 1;
		}
	} else if (status_get_range(src) > 3)
		wd.flag=(wd.flag&~BF_RANGEMASK)|BF_LONG;

	if(skill_num){
		wd.flag=(wd.flag&~BF_SKILLMASK)|BF_SKILL;
		switch(skill_num)
		{		
			case AC_DOUBLE:
			case AC_SHOWER:
			case AC_CHARGEARROW:
			case BA_MUSICALSTRIKE:
			case DC_THROWARROW:
			case CG_ARROWVULCAN:
				wd.flag=(wd.flag&~BF_RANGEMASK)|BF_LONG;
				flag.arrow = 1;
				break;

			case MO_FINGEROFFENSIVE:
				if(sd && battle_config.finger_offensive_type == 0)
					wd.div_ = sd->spiritball_old;
			case KN_SPEARBOOMERANG:
			case NPC_RANGEATTACK:
			case CR_SHIELDBOOMERANG:
			case LK_SPIRALPIERCE:
			case ASC_BREAKER:
			case PA_SHIELDCHAIN:
			case CR_GRANDCROSS:	//GrandCross really shouldn't count as short-range, aight?
				wd.flag=(wd.flag&~BF_RANGEMASK)|BF_LONG;
				break;
			case KN_PIERCE:
				wd.div_= t_size+1;
				break;

			case KN_SPEARSTAB:
			case KN_BOWLINGBASH:
				wd.blewcount=0;
				break;

			case NPC_PIERCINGATT:
			case CR_SHIELDCHARGE:
				wd.flag=(wd.flag&~BF_RANGEMASK)|BF_SHORT;
				break;

			case KN_AUTOCOUNTER:
				wd.flag=(wd.flag&~BF_SKILLMASK)|BF_NORMAL;
				break;
		}
	}

	if(is_boss(target)) //Bosses can't be knocked-back
		wd.blewcount = 0;

	if (sd)
	{	//Arrow consumption
		sd->state.arrow_atk = flag.arrow;
	}

	//Check for counter 
	if(skill_num != CR_GRANDCROSS &&
 		(!skill_num ||
		(tsd && battle_config.pc_auto_counter_type&2) ||
		(tmd && battle_config.monster_auto_counter_type&2)))
	{
		if(t_sc_data && t_sc_data[SC_AUTOCOUNTER].timer != -1)
		{
			int dir = map_calc_dir(*src,target->x,target->y),t_dir = status_get_dir(target);
			int dist = distance(src->x,src->y,target->x,target->y);
			if(dist <= 0 || map_check_dir(dir,t_dir) )
			{
				memset(&wd,0,sizeof(wd));
				t_sc_data[SC_AUTOCOUNTER].val3 = 0;
				t_sc_data[SC_AUTOCOUNTER].val4 = 1;
				if(sc_data && sc_data[SC_AUTOCOUNTER].timer == -1)
				{ //How can the attacking char have Auto-counter active?
					int range = status_get_range(target);
					if((tsd && tsd->status.weapon != 11 && dist <= range+1) ||
						(tmd && range <= 3 && dist <= range+1))
						t_sc_data[SC_AUTOCOUNTER].val3 = src->id;
				}
				return wd;
			} else
				flag.cri = 1;
		}
		else if(t_sc_data && t_sc_data[SC_POISONREACT].timer != -1)
		{	// poison react [Celest]
			t_sc_data[SC_POISONREACT].val3 = 0;
			t_sc_data[SC_POISONREACT].val4 = 1;
			t_sc_data[SC_POISONREACT].val3 = src->id;
		}
	}	//End counter-check

	if(sd && !skill_num && !flag.cri)
	{	//Check for conditions that convert an attack to a skill
		char da=0;
		skill = 0;
		if((sd->weapontype1 == 0x01 && (skill = pc_checkskill(*sd,TF_DOUBLE)) > 0) ||
			sd->double_rate > 0) //success rate from Double Attack is counted in
			da = (rand()%100 <  sd->double_rate + 5*skill) ? 1:0;
		if((skill = pc_checkskill(*sd,MO_TRIPLEATTACK)) > 0 && sd->status.weapon <= 16) // triple blow works with bows ^^ [celest]
			da = (rand()%100 < (30 - skill)) ? 2:da;
		
		if (da == 1)
		{
			skill_num = TF_DOUBLE;
			wd.div_ = 2;
		} else if (da == 2) {
			skill_num = MO_TRIPLEATTACK;
			skill_lv = skill;
			wd.div_ = 255;
		}
		
		if (da)
			wd.type = 0x08;
	}

	if (!skill_num && !flag.cri && sc_data && sc_data[SC_SACRIFICE].timer != -1)
	{
		skill_num = PA_SACRIFICE;
		skill_lv =  sc_data[SC_SACRIFICE].val1;
	}

	if (!skill_num && (tsd || battle_config.enemy_perfect_flee))
	{	//Check for Lucky Dodge
		short flee2 = status_get_flee2(target);
		if (rand()%1000 < flee2)
		{
			wd.type=0x0b;
			wd.dmg_lv=ATK_LUCKY;
			return wd;
		}
	}

	//Initialize variables that will be used afterwards
	if (sd)
	{
		t_race = status_get_race(target);
		t_ele = status_get_elem_type(target);
	}
	if (tsd)
	{
		s_race = status_get_race(src);
	}
	s_ele=status_get_attack_element(src);
	s_ele_=status_get_attack_element2(src);

	if (flag.arrow && sd && sd->arrow_ele)
		s_ele = sd->arrow_ele;

	if (sd)
	{	//Set whether damage1 or damage2 (or both) will be used
		if(sd->weapontype1 == 0 && sd->weapontype2 > 0)
			{
				flag.rh=0;
				flag.lh=1;
			}
		if(sd->status.weapon > 16)
			flag.rh = flag.lh = 1;
	}

	//Check for critical
	if(!flag.cri &&
		(sd || battle_config.enemy_critical) &&
		(!skill_num || skill_num == KN_AUTOCOUNTER || skill_num == SN_SHARPSHOOTING))
	{
		short cri = status_get_critical(src);
		if (sd)
		{
			cri+= sd->critaddrace[t_race];
			if(flag.arrow)
				cri += sd->arrow_cri;
			if(sd->status.weapon == 16)
				cri <<=1;
		}
		//The official equation is *2, but that only applies when sd's do critical.
		//Therefore, we use the old value 3 on cases when an sd gets attacked by a mob
		cri -= status_get_luk(target) * (md&&tsd?3:2);
		if(!sd && battle_config.enemy_critical_rate != 100)
		{ //Mob/Pets
			cri = cri*battle_config.enemy_critical_rate/100;
			if (cri<1) cri = 1;
		}
		
		if(t_sc_data)
		{
			if (t_sc_data[SC_SLEEP].timer!=-1 )
				cri <<=1;
			if(t_sc_data[SC_JOINTBEAT].timer != -1 &&
				t_sc_data[SC_JOINTBEAT].val2 == 6) // Always take crits with Neck broken by Joint Beat [DracoRPG]
				flag.cri=1;
		}
		switch (skill_num)
		{
			case KN_AUTOCOUNTER:
				if(!(battle_config.pc_auto_counter_type&1))
					flag.cri = 1;
				else
					cri <<= 1;
				break;
			case SN_SHARPSHOOTING:
				cri += 200;
				break;
		}
		if(tsd && tsd->critical_def)
			cri = cri*(100-tsd->critical_def)/100;
		if (rand()%1000 < cri)
			flag.cri= 1;
	}
	if (flag.cri)
	{
		wd.type = 0x0a;
		flag.idef = flag.idef2 = flag.hit = 1;
	} else {	//Check for Perfect Hit
		if(sd && sd->perfect_hit > 0 && rand()%100 < sd->perfect_hit)
			flag.hit = 1;
		if (skill_num && !flag.hit)
			switch(skill_num)
		{
			case NPC_GUIDEDATTACK:
			case RG_BACKSTAP:
			case CR_GRANDCROSS:
			case AM_ACIDTERROR:
			case MO_INVESTIGATE:
			case MO_EXTREMITYFIST:
			case PA_PRESSURE:
			case PA_SACRIFICE:
				flag.hit = 1;
				break;
		}
		if ((t_sc_data && !flag.hit) &&
			(t_sc_data[SC_SLEEP].timer!=-1 ||
			t_sc_data[SC_STAN].timer!=-1 ||
			t_sc_data[SC_FREEZE].timer!=-1 ||
			(t_sc_data[SC_STONE].timer!=-1 && t_sc_data[SC_STONE].val2==0))
			)
			flag.hit = 1;
	}

	if (!flag.hit)
	{	//Hit/Flee calculation
		unsigned short flee = status_get_flee(target);
		unsigned short hitrate=80; //Default hitrate
		if(battle_config.agi_penalty_type)
		{	
			unsigned char target_count; //256 max targets should be a sane max
			target_count = 1+battle_counttargeted(*target,src,battle_config.agi_penalty_count_lv);
			if(target_count >= battle_config.agi_penalty_count)
			{
				if (battle_config.agi_penalty_type == 1)
					flee = (flee * (100 - (target_count - (battle_config.agi_penalty_count - 1))*battle_config.agi_penalty_num))/100;
				else //asume type 2: absolute reduction
					flee -= (target_count - (battle_config.agi_penalty_count - 1))*battle_config.agi_penalty_num;
				if(flee < 1) flee = 1;
			}
		}

		hitrate+= status_get_hit(src) - flee;
		
		if(sd)
		{	
			if (flag.arrow)
				hitrate += sd->arrow_hit;
			// weapon research hidden bonus
			if ((skill = pc_checkskill(*sd,BS_WEAPONRESEARCH)) > 0)
				hitrate += hitrate * (2*skill)/100;
		}
		if(skill_num)
			switch(skill_num)
		{	//Hit skill modifiers
			case SM_BASH:
				hitrate += (skill_lv>5?20:10);
				break;
			case SM_MAGNUM:
				hitrate += hitrate*(10*skill_lv)/100;
				break;
			case KN_AUTOCOUNTER:
				hitrate += 20;
				break;
			case KN_PIERCE:
				hitrate += hitrate*(5*skill_lv)/100;
				break;
			case PA_SHIELDCHAIN:
				hitrate += 20;
				break;
		}

		if (hitrate > battle_config.max_hitrate)
			hitrate = battle_config.max_hitrate;
		else if (hitrate < battle_config.min_hitrate)
			hitrate = battle_config.min_hitrate;

		if(rand()%100 >= hitrate)
			wd.dmg_lv = ATK_FLEE;
		else
			flag.hit =1;
	}	//End hit/miss calculation

	if(tsd && tsd->state.no_weapon_damage && skill_num != CR_GRANDCROSS)	
		return wd;

	if (flag.hit && !(t_mode&0x40)) //No need to do the math for plants
	{	//Hitting attack

//Assuming that 99% of the cases we will not need to check for the flag.rh... we don't.
//ATK_RATE scales the damage. 100 = no change. 50 is halved, 200 is doubled, etc
#define ATK_RATE( a ) { wd.damage= wd.damage*(a)/100 ; if(flag.lh) wd.damage2= wd.damage2*(a)/100; }
#define ATK_RATE2( a , b ) { wd.damage= wd.damage*(a)/100 ; if(flag.lh) wd.damage2= wd.damage2*(b)/100; }
//Adds dmg%. 100 = +100% (double) damage. 10 = +10% damage
#define ATK_ADDRATE( a ) { wd.damage+= wd.damage*(a)/100 ; if(flag.lh) wd.damage2+= wd.damage2*(a)/100; }
#define ATK_ADDRATE2( a , b ) { wd.damage+= wd.damage*(a)/100 ; if(flag.lh) wd.damage2+= wd.damage2*(b)/100; }
//Adds an absolute value to damage. 100 = +100 damage
#define ATK_ADD( a ) { wd.damage+= a; if (flag.lh) wd.damage2+= a; }
#define ATK_ADD2( a , b ) { wd.damage+= a; if (flag.lh) wd.damage2+= b; }

		if (status_get_def(target) >= 1000000)
			flag.infdef =1;
		def1 = status_get_def(target);
		def2 = status_get_def2(target);
		
		switch (skill_num)
		{	//Calc base damage according to skill
			case PA_SACRIFICE:
				ATK_ADD(status_get_max_hp(src)* 9/100);
				break;
			case PA_PRESSURE: //Since PRESSURE ignores everything, finish here
				wd.damage=battle_calc_damage(src,target,500+300*skill_lv,wd.div_,skill_num,skill_lv,wd.flag);
				wd.damage2=0;
				return wd;	
			default:
			{
				unsigned short baseatk=0, baseatk_=0, atkmin=0, atkmax=0, atkmin_=0, atkmax_=0;
				if (!sd)
				{	//Mobs/Pets
					if ((md && battle_config.enemy_str) ||
						(pd && battle_config.pet_str))
						baseatk = status_get_baseatk(src);

					if(skill_num==HW_MAGICCRASHER)
					{		  
						if (!flag.cri)
							atkmin = status_get_matk1(src);
						atkmax = status_get_matk2(src);
					} else {
						if (!flag.cri)
							atkmin = status_get_atk(src);
						atkmax = status_get_atk2(src);
					}
					if (atkmin > atkmax)
						atkmin = atkmax;
				} else {	//PCs
					if(skill_num==HW_MAGICCRASHER)
					{
						baseatk = status_get_matk1(src);
						if (flag.lh) baseatk_ = baseatk;
					} else { 
						baseatk = status_get_baseatk(src);
						if (flag.lh) baseatk_ = baseatk;
					}
					//rodatazone says that Overrefine bonuses are part of baseatk
					if(sd->right_weapon.overrefine>0)
						baseatk+= rand()%sd->right_weapon.overrefine+1;
					if (flag.lh && sd->left_weapon.overrefine>0)
						baseatk_+= rand()%sd->left_weapon.overrefine+1;
					
					atkmax = status_get_atk(src);
					if (flag.lh)
						atkmax_ = status_get_atk(src);

					if (!flag.cri)
					{	//Normal attacks
						atkmin = atkmin_ = status_get_dex(src);
						
						if (sd->equip_index[9] < MAX_INVENTORY && sd->inventory_data[sd->equip_index[9]])
							atkmin = atkmin*(80 + sd->inventory_data[sd->equip_index[9]]->wlv*20)/100;
						
						if (atkmin > atkmax)
							atkmin = atkmax;
						
						if(flag.lh)
						{
							if (sd->equip_index[8] < MAX_INVENTORY && sd->inventory_data[sd->equip_index[8]])
								atkmin_ = atkmin_*(80 + sd->inventory_data[sd->equip_index[8]]->wlv*20)/100;
						
							if (atkmin_ > atkmax_)
								atkmin_ = atkmax_;
						}
						
						if(sd->status.weapon == 11)
						{	//Bows
							atkmin = atkmin*atkmax/100;
							if (atkmin > atkmax)
								atkmax = atkmin;
						}
					}
				}
				
				if (sc_data && sc_data[SC_MAXIMIZEPOWER].timer!=-1)
				{
					atkmin = atkmax;
					atkmin_ = atkmax_;
				}
				//Weapon Damage calculation
				//Store watk in wd.damage to use the above defines for easy handling, and then add baseatk
				if (!flag.cri)
				{
					ATK_ADD2((atkmax>atkmin? rand()%(atkmax-atkmin) :0) +atkmin,
						(atkmax_>atkmin_? rand()%(atkmax_-atkmin_) :0) +atkmin_);
				} else 
					ATK_ADD2(atkmax, atkmax_);
				
				if (sd)
				{
					//rodatazone says the range is 0~arrow_atk-1 for non crit
					if (flag.arrow && sd->arrow_atk)
						ATK_ADD(flag.cri?sd->arrow_atk:rand()%sd->arrow_atk);
					
					if(sd->status.weapon < 16 && (sd->atk_rate != 100 || sd->weapon_atk_rate != 0))
						ATK_RATE(sd->atk_rate + sd->weapon_atk_rate[sd->status.weapon]);

					if(flag.cri && sd->crit_atk_rate)
						ATK_ADDRATE(sd->crit_atk_rate);
				
					//SizeFix only for players
					if (!(
						/*!tsd || //rodatazone claims that target human players don't have a size! -- I really don't believe it... removed until we find some evidence*/
						sd->state.no_sizefix ||
						(sc_data && sc_data[SC_WEAPONPERFECTION].timer!=-1) ||
						(pc_isriding(*sd) && (sd->status.weapon==4 || sd->status.weapon==5) && t_size==1) ||
						(skill_num == MO_EXTREMITYFIST)
						))
					{
						ATK_RATE2(sd->right_weapon.atkmods[t_size], sd->left_weapon.atkmods[t_size]);
					}
				}
				//Finally, add baseatk
				ATK_ADD2(baseatk, baseatk_);
				break;
			}	//End default case
		} //End switch(skill_num)

		//Skill damage modifiers
		if(sc_data && skill_num != PA_SACRIFICE)
		{
			if(sc_data[SC_OVERTHRUST].timer!=-1)
				skillratio += 5*sc_data[SC_OVERTHRUST].val1;
			if(sc_data[SC_TRUESIGHT].timer!=-1)
				skillratio += 2*sc_data[SC_TRUESIGHT].val1;
			if(sc_data[SC_BERSERK].timer!=-1)
				skillratio += 100; // Although RagnaInfo says +200%, it's *200% so +100%
			if(sc_data[SC_MAXOVERTHRUST].timer!=-1)
				skillratio += 20*sc_data[SC_MAXOVERTHRUST].val1;
			if(sc_data[SC_EDP].timer != -1 &&
				skill_num != AS_SPLASHER &&
				skill_num != ASC_BREAKER &&
				skill_num != ASC_METEORASSAULT)
			{	
				skillratio += 150 + sc_data[SC_EDP].val1 * 50;
				flag.cardfix = 0;
			}
		}
		if (!skill_num)
		{
			//Executioner card addition - Consider it as part of skill-based-damage
			if(sd &&
				sd->random_attack_increase_add > 0 &&
				sd->random_attack_increase_per &&
				rand()%100 < sd->random_attack_increase_per
				)
				skillratio += sd->random_attack_increase_add;
		
			ATK_RATE(skillratio);
		} else {	//Skills
			char ele_flag=0;	//For skills that force the neutral element.

			switch( skill_num )
			{
				case SM_BASH:
					skillratio+= 30*skill_lv;
					break;
				case SM_MAGNUM:
					// 20*skill level+100? I think this will do for now [based on jRO info]
					skillratio+= (wflag > 1 ? 5*skill_lv+15 : 30*skill_lv);
					break;
				case MC_MAMMONITE:
					skillratio+= 50*skill_lv;
					break;
				case AC_DOUBLE:
					skillratio+= 80+ 20*skill_lv;
					break;
				case AC_SHOWER:
					skillratio+= 5*skill_lv -25;
					break;
				case AC_CHARGEARROW:
					skillratio+= 50;
					break;
				case KN_PIERCE:
					skillratio+= wd.div_*(100+10*skill_lv) -100;
					//div_flag=1;
					break;
				case KN_SPEARSTAB:
					skillratio+= 15*skill_lv;
					break;
				case KN_SPEARBOOMERANG:
					skillratio+= 50*skill_lv;
					break;
				case KN_BRANDISHSPEAR:
					skillratio+=20*skill_lv;
					if(skill_lv>3 && wflag==1) skillratio+= 50;
					if(skill_lv>6 && wflag==1) skillratio+= 25;
					if(skill_lv>9 && wflag==1) skillratio+= 12; //1/8th = 12.5%, rounded to 12?
					if(skill_lv>6 && wflag==2) skillratio+= 50;
					if(skill_lv>9 && wflag==2) skillratio+= 25;
					if(skill_lv>9 && wflag==3) skillratio+= 50;
				case KN_BOWLINGBASH:
					skillratio+= 50*skill_lv;
					break;
				case KN_AUTOCOUNTER:
					flag.idef= flag.idef2= 1;
					break;
				case TF_DOUBLE:
					skillratio += 100;
					break;
				case AS_GRIMTOOTH:
					skillratio+= 20*skill_lv;
					break;
				case AS_POISONREACT: // celest
					skillratio+= 30*skill_lv;
					ele_flag=1;
					break;
				case AS_SONICBLOW:
					skillratio+= 200+ 50*skill_lv;
					break;
				case TF_SPRINKLESAND:
					skillratio+= 25;
					break;
				case MC_CARTREVOLUTION:
					if(sd && sd->cart_max_weight > 0 && sd->cart_weight > 0)
						skillratio+= 50+sd->cart_weight/800; // +1% every 80 weight units
					else
						skillratio+= 50;
					break;
				case NPC_COMBOATTACK:
						skillratio += 100*wd.div_ -100;
						//div_flag=1;
					break;
				case NPC_RANDOMATTACK:
					skillratio+= rand()%150-50;
					break;
				case NPC_WATERATTACK:
				case NPC_GROUNDATTACK:
				case NPC_FIREATTACK:
				case NPC_WINDATTACK:
				case NPC_POISONATTACK:
				case NPC_HOLYATTACK:
				case NPC_DARKNESSATTACK:
				case NPC_UNDEADATTACK:
				case NPC_TELEKINESISATTACK:
					skillratio+= 25*skill_lv;
					break;
				case NPC_GUIDEDATTACK:
				case NPC_RANGEATTACK:
				case NPC_PIERCINGATT:
					break;
				case NPC_CRITICALSLASH:
					flag.idef= flag.idef2= 1;
					break;
				case RG_BACKSTAP:
					if(sd && sd->status.weapon == 11 && battle_config.backstab_bow_penalty)
						skillratio+= (200+ 40*skill_lv)/2;
					else
						skillratio+= 200+ 40*skill_lv;
					break;
				case RG_RAID:
					skillratio+= 40*skill_lv;
					break;
				case RG_INTIMIDATE:
					skillratio+= 30*skill_lv;
					break;
				case CR_SHIELDCHARGE:
					skillratio+= 20*skill_lv;
					ele_flag=1;
					break;
				case CR_SHIELDBOOMERANG:
					skillratio+= 30*skill_lv;
					ele_flag=1;
					break;
				case CR_HOLYCROSS:
					skillratio+= 35*skill_lv;
					break;
				case CR_GRANDCROSS:
					if(!battle_config.gx_cardfix)
						flag.cardfix = 0;
					break;
				case AM_DEMONSTRATION:
					skillratio+= 20*skill_lv;
					flag.cardfix = 0;
					break;
				case AM_ACIDTERROR:
					skillratio+= 40*skill_lv;
					ele_flag=1;
					flag.idef = flag.idef2= 1;
					flag.cardfix = 0;
					break;
				case MO_FINGEROFFENSIVE:
					if(battle_config.finger_offensive_type == 0)
						//div_flag = 1;
						skillratio+= wd.div_ * (125 + 25*skill_lv) -100;
					else
						skillratio+= 25 + 25 * skill_lv;
					break;
				case MO_INVESTIGATE:
					if (!flag.infdef)
					{
						skillratio+=75*skill_lv;
						ATK_RATE((def1 + def2)/2);
					}
					flag.idef= flag.idef2= 1;
					break;
				case MO_EXTREMITYFIST:
					if (sd)
					{	//Overflow check. [Skotlex]
						int ratio = skillratio + 100*(8 + ((sd->status.sp)/10));
						//You'd need something like 6K SP to reach this max, so should be fine for most purposes.
						if (ratio > 60000) ratio = 60000; //We leave some room here in case skill_ratio gets further increased.
						skillratio = ratio;
						sd->status.sp = 0;
						clif_updatestatus(*sd,SP_SP);
					}
					flag.idef= flag.idef2= 1;
					ele_flag=1;
					break;
				case MO_TRIPLEATTACK:
					skillratio+= 20*skill_lv;
					break;
				case MO_CHAINCOMBO:
					skillratio+= 50+ 50*skill_lv;
					break;
				case MO_COMBOFINISH:
					skillratio+= 140+ 60*skill_lv;
					break;
				case BA_MUSICALSTRIKE:
					skillratio+= 40*skill_lv -40;
					break;
				case DC_THROWARROW:
					skillratio+= 50*skill_lv;
					break;
				case CH_TIGERFIST:
					skillratio+= 100*skill_lv-60;
					break;
				case CH_CHAINCRUSH:
					skillratio+= 300+ 100*skill_lv;
					break;
				case CH_PALMSTRIKE:
					skillratio+= 100+ 100*skill_lv;
					break;
				case LK_SPIRALPIERCE:
					skillratio+=50*skill_lv;
					flag.idef= flag.idef2= 1;
					if(tsd)
						tsd->canmove_tick = gettick() + 1000;
					else if(tmd)
						tmd->canmove_tick = gettick() + 1000;
					break;
				case LK_HEADCRUSH:
					skillratio+=40*skill_lv;
					break;
				case LK_JOINTBEAT:
					skillratio+= 10*skill_lv-50;
					break;
				case ASC_METEORASSAULT:
					skillratio+= 40*skill_lv-60;
					break;
				case SN_SHARPSHOOTING:
					skillratio+= 100+50*skill_lv;
					break;
				case CG_ARROWVULCAN:
					skillratio+= 100+100*skill_lv;
					break;
				case AS_SPLASHER:
					skillratio+= 100+20*skill_lv;
					if (sd)
						skillratio+= 20*pc_checkskill(*sd,AS_POISONREACT);
					flag.cardfix = 0;
					break;
				case ASC_BREAKER:
					skillratio+= 100*skill_lv -100;
					break;
				case PA_SACRIFICE:
					skillratio+= 10*skill_lv -10;
					flag.idef = flag.idef2 = 1;
					ele_flag=1;
					break;
				case PA_SHIELDCHAIN:
					skillratio+= wd.div_*(100+30*skill_lv)-100;
					//div_flag=1;
					ele_flag=1;
					break;
				case WS_CARTTERMINATION:
					if(sd && sd->cart_max_weight && sd->cart_weight > 0) //Why check for cart_max_weight? It is not used!
						skillratio += sd->cart_weight / (10 * (16 - skill_lv)) - 100;
					else if (!sd)
						skillratio += 80000 / (10 * (16 - skill_lv));
					break;
				case CR_ACIDDEMONSTRATION:
					skillratio += wd.div_*100 - 100;
					break;
			}
			if (ele_flag)
				s_ele=s_ele_=0;
			else if((skill=skill_get_pl(skill_num))>0) //Checking for the skill's element
				s_ele=s_ele_=skill;

			if (sd && sd->skillatk[0] == skill_num)
				//If we apply skillatk[] as ATK_RATE, it will also affect other skills,
				//unfortunately this way ignores a skill's constant modifiers...
				skillratio += sd->skillatk[1];

			//Double attack does not applies to left hand
			ATK_RATE2(skillratio, skillratio - (skill_num==TF_DOUBLE?100:0));

			//Constant/misc additions from skills
			if (skill_num == MO_EXTREMITYFIST)
				ATK_ADD(250 + 150*skill_lv);

			if (sd)
			{
				short index= 0;
				switch (skill_num)
				{
					case	PA_SACRIFICE:
						pc_heal(*sd, -wd.damage, 0);//Do we really always use wd.damage here?
						//clif_skill_nodamage(*src,*target,skill_num,skill_lv,1);  // this doesn't show effect either.. hmm =/
						sc_data[SC_SACRIFICE].val2 --;
						if (sc_data[SC_SACRIFICE].val2 == 0)
							status_change_end(src, SC_SACRIFICE,-1);
						break;
					case CR_SHIELDBOOMERANG:
					case PA_SHIELDCHAIN:
						if ((index = sd->equip_index[8]) < MAX_INVENTORY &&
							sd->inventory_data[index] &&
							sd->inventory_data[index]->type == 5)
						{
							ATK_ADD(sd->inventory_data[index]->weight/10);
							ATK_ADD(sd->status.inventory[index].refine * status_getrefinebonus(0,1));
						}
						break;
					case LK_SPIRALPIERCE:
						if ((index = sd->equip_index[9]) < MAX_INVENTORY &&
							sd->inventory_data[index] &&
							sd->inventory_data[index]->type == 4)
						{
							ATK_ADD((int)(double)(sd->inventory_data[index]->weight*(0.8*skill_lv*4/10)));
							ATK_ADD(sd->status.inventory[index].refine * status_getrefinebonus(0,1));
						}
						break;
				}	//switch
			}	//if (sd)
		}

		if(sd)
		{
			if (skill_num != PA_SACRIFICE && skill_num != MO_INVESTIGATE && !flag.cri && !flag.infdef)
			{	//Elemental/Racial adjustments
				char raceele_flag=0, raceele_flag_=0;
				if(sd->right_weapon.def_ratio_atk_ele & (1<<t_ele) ||
					sd->right_weapon.def_ratio_atk_race & (1<<t_race) ||
					sd->right_weapon.def_ratio_atk_race & (is_boss(target)?1<<10:1<<11)
					)
					raceele_flag = flag.idef = 1;
				
				if(sd->left_weapon.def_ratio_atk_ele & (1<<t_ele) ||
					sd->left_weapon.def_ratio_atk_race & (1<<t_race) ||
					sd->left_weapon.def_ratio_atk_race & (is_boss(target)?1<<10:1<<11)
					)
					raceele_flag_ = flag.idef2 = 1;
				
				if (raceele_flag || raceele_flag_)
					ATK_RATE2(raceele_flag?(def1 + def2):100, raceele_flag_?(def1 + def2):100);
			}

			//Ignore Defense?
			if (!flag.idef && (
				(tmd && sd->right_weapon.ignore_def_mob & (is_boss(target)?2:1)) ||
				sd->right_weapon.ignore_def_ele & (1<<t_ele) ||
				sd->right_weapon.ignore_def_race & (1<<t_race) ||
				sd->right_weapon.ignore_def_race & (is_boss(target)?1<<10:1<<11)
				))
				flag.idef = 1;

			if (!flag.idef2 && (
				(tmd && sd->left_weapon.ignore_def_mob & (is_boss(target)?2:1)) ||
				sd->left_weapon.ignore_def_ele & (1<<t_ele) ||
				sd->left_weapon.ignore_def_race & (1<<t_race) ||
				sd->left_weapon.ignore_def_race & (is_boss(target)?1<<10:1<<11)
				))
				flag.idef2 = 1;
		}

		if (!flag.infdef && (!flag.idef || !flag.idef2))
		{	//Defense reduction
			int t_vit = status_get_vit(target);
			int vit_def;

			if(battle_config.vit_penalty_type)
			{
				unsigned char target_count; //256 max targets should be a sane max
				target_count = 1 + battle_counttargeted(*target,src,battle_config.vit_penalty_count_lv);
				if(target_count >= battle_config.vit_penalty_count) {
					if(battle_config.vit_penalty_type == 1) {
						def1 = (def1 * (100 - (target_count - (battle_config.vit_penalty_count - 1))*battle_config.vit_penalty_num))/100;
						def2 = (def2 * (100 - (target_count - (battle_config.vit_penalty_count - 1))*battle_config.vit_penalty_num))/100;
						t_vit = (t_vit * (100 - (target_count - (battle_config.vit_penalty_count - 1))*battle_config.vit_penalty_num))/100;
					} else { //Assume type 2
						def1 -= (target_count - (battle_config.vit_penalty_count - 1))*battle_config.vit_penalty_num;
						def2 -= (target_count - (battle_config.vit_penalty_count - 1))*battle_config.vit_penalty_num;
						t_vit -= (target_count - (battle_config.vit_penalty_count - 1))*battle_config.vit_penalty_num;
					}
				}
				if(def1 < 0) def1 = 0;
				if(def2 < 1) def2 = 1;
				if(t_vit < 1) t_vit = 1;
			}
			//Vitality reduction from rodatazone: http://rodatazone.simgaming.net/mechanics/substats.php#def	
			if (tsd)	//Sd vit-eq
			{	//[VIT*0.5] + rnd([VIT*0.3], max([VIT*0.3],[VIT^2/150]-1))
				vit_def = t_vit*(t_vit-15)/150;
				vit_def = t_vit/2 + (vit_def>0?rand()%vit_def:0);
				
				if((battle_check_undead(s_race,status_get_elem_type(src)) || s_race==6) &&
				(skill=pc_checkskill(*tsd,AL_DP)) >0)
					vit_def += skill*(3 +(tsd->status.base_level+1)/25);   // submitted by orn
			} else { //Mob-Pet vit-eq
				//VIT + rnd(0,[VIT/20]^2-1)
				vit_def = (t_vit/20)*(t_vit/20);
				vit_def = t_vit + (vit_def>0?rand()%vit_def:0);
			}
			
			if(battle_config.player_defense_type)
				vit_def += def1*battle_config.player_defense_type;
			else
				ATK_RATE2(flag.idef?100:100-def1, flag.idef2?100:100-def1);
			ATK_ADD2(flag.idef?0:-vit_def, flag.idef2?0:-vit_def);
		}
		//Post skill/vit reduction damage increases
		if (sc_data)
		{	//SC skill damages
			if(sc_data[SC_AURABLADE].timer!=-1) 
				ATK_ADD(20*sc_data[SC_AURABLADE].val1);
		}

		if (sd && skill_num != MO_INVESTIGATE && skill_num != MO_EXTREMITYFIST)
		{	//refine bonus
			ATK_ADD2(status_get_atk2(src), status_get_atk_2(src));
			//Items forged from the Top 10 most famous BS's get 10 dmg bonus
			ATK_ADD2(sd->right_weapon.fameflag*10, sd->left_weapon.fameflag*10);
		}

		//Set to min of 1
		if (flag.rh && wd.damage < 1) wd.damage = 1;
		if (flag.lh && wd.damage2 < 1) wd.damage2 = 1;

		if (sd && skill_num != MO_INVESTIGATE && skill_num != MO_EXTREMITYFIST && skill_num != CR_GRANDCROSS)
		{	//Add mastery damage
			wd.damage = battle_addmastery(sd,target,wd.damage,0);
			if (flag.lh) wd.damage2 = battle_addmastery(sd,target,wd.damage2,1);
		}
	} //Here ends flag.hit section, the rest of the function applies to both hitting and missing attacks

	if(sd && (skill=pc_checkskill(*sd,BS_WEAPONRESEARCH)) > 0)
		ATK_ADD(skill*2);

	if(skill_num==TF_POISON)
		ATK_ADD(15*skill_lv);

	if (sd ||
		(md && !skill_num && !battle_config.mob_attack_attr_none) ||
		(pd && !skill_num && !battle_config.pet_attack_attr_none))
	{	//Elemental attribute fix
		if	(!(!sd && tsd && !battle_config.mob_ghostring_fix && t_ele==8))
		{
			short t_element = status_get_element(target);
			if (wd.damage > 0)
			{
				wd.damage=battle_attr_fix(wd.damage,s_ele,t_element);
				if(skill_num==MC_CARTREVOLUTION) //Cart Revolution applies the element fix once more with neutral element
					wd.damage=battle_attr_fix(wd.damage,0,t_element);
			}
			if (flag.lh && wd.damage2 > 0) wd.damage2=battle_attr_fix(wd.damage2,s_ele_,t_element);
		}
	}

	if ((!flag.rh || wd.damage == 0) && (!flag.lh || wd.damage2 == 0))
		flag.cardfix = 0;	//When the attack does no damage, avoid doing %bonuses

	if (sd)
	{
		ATK_ADD2(sd->right_weapon.star, sd->left_weapon.star);
		ATK_ADD(sd->spiritball*3);

		//Card Fix, sd side
		if (flag.cardfix)
		{
			short cardfix = 1000, cardfix_ = 1000;
			short t_class = status_get_class(target);
			short t_race2 = status_get_race2(target);	
			if(sd->state.arrow_atk)
			{
				cardfix=cardfix*(100+sd->right_weapon.addrace[t_race]+sd->arrow_addrace[t_race])/100;
				cardfix=cardfix*(100+sd->right_weapon.addele[t_ele]+sd->arrow_addele[t_ele])/100;
				cardfix=cardfix*(100+sd->right_weapon.addsize[t_size]+sd->arrow_addsize[t_size])/100;
				cardfix=cardfix*(100+sd->right_weapon.addrace2[t_race2])/100;
				cardfix=cardfix*(100+sd->right_weapon.addrace[is_boss(target)?10:11]+sd->arrow_addrace[t_mode & 0x20?10:11])/100;
			} else {	//Melee attack
				if(!battle_config.left_cardfix_to_right)
				{
					cardfix=cardfix*(100+sd->right_weapon.addrace[t_race])/100;
					cardfix=cardfix*(100+sd->right_weapon.addele[t_ele])/100;
					cardfix=cardfix*(100+sd->right_weapon.addsize[t_size])/100;
					cardfix=cardfix*(100+sd->right_weapon.addrace2[t_race2])/100;
					cardfix=cardfix*(100+sd->right_weapon.addrace[is_boss(target)?10:11])/100;

					if (flag.lh)
					{
						cardfix_=cardfix_*(100+sd->left_weapon.addrace[t_race])/100;
						cardfix_=cardfix_*(100+sd->left_weapon.addele[t_ele])/100;
						cardfix_=cardfix_*(100+sd->left_weapon.addsize[t_size])/100;
						cardfix_=cardfix_*(100+sd->left_weapon.addrace2[t_race2])/100;
						cardfix_=cardfix_*(100+sd->left_weapon.addrace[is_boss(target)?10:11])/100;
					}
				} else {
					cardfix=cardfix*(100+sd->right_weapon.addrace[t_race]+sd->left_weapon.addrace[t_race])/100;
					cardfix=cardfix*(100+sd->right_weapon.addele[t_ele]+sd->left_weapon.addele[t_ele])/100;
					cardfix=cardfix*(100+sd->right_weapon.addsize[t_size]+sd->left_weapon.addsize[t_size])/100;
					cardfix=cardfix*(100+sd->right_weapon.addrace2[t_race2]+sd->left_weapon.addrace2[t_race2])/100;
					cardfix=cardfix*(100+sd->right_weapon.addrace[is_boss(target)?10:11]+sd->left_weapon.addrace[t_mode & 0x20?10:11])/100;
				}
			}

			for(i=0;i<sd->right_weapon.add_damage_class_count;i++) {
				if(sd->right_weapon.add_damage_classid[i] == t_class) {
					cardfix=cardfix*(100+sd->right_weapon.add_damage_classrate[i])/100;
					break;
				}
			}

			if (flag.lh)
			{
				for(i=0;i<sd->left_weapon.add_damage_class_count;i++) {
					if(sd->left_weapon.add_damage_classid[i] == t_class) {
						cardfix_=cardfix_*(100+sd->left_weapon.add_damage_classrate[i])/100;
						break;
					}
				}
			}

			if (cardfix != 1000 || cardfix_ != 1000)
				ATK_RATE2(cardfix/10, cardfix_/10);	//What happens if you use right-to-left and there's no right weapon, only left?
		}
	} //if (sd)

	//Card Fix, tsd side
	if (tsd && flag.cardfix) {
		short s_size,s_race2,s_mode,s_class;
		short cardfix=1000;
		
		s_size = status_get_size(src);
		s_race2 = status_get_race2(src);
		s_mode = status_get_mode(src);
		s_class = status_get_class(src);
		
		cardfix=cardfix*(100-tsd->subrace[s_race])/100;
		cardfix=cardfix*(100-tsd->subele[s_ele])/100;
		cardfix=cardfix*(100-tsd->subsize[s_size])/100;
 		cardfix=cardfix*(100-tsd->subrace2[s_race2])/100;
		cardfix=cardfix*(100-tsd->subrace[is_boss(target)?10:11])/100;
		
		for(i=0;i<tsd->add_damage_class_count2;i++) {
				if(tsd->add_damage_classid2[i] == s_class) {
					cardfix=cardfix*(100+tsd->add_damage_classrate2[i])/100;
					break;
				}
			}
	
		if(wd.flag&BF_SHORT)
			cardfix=cardfix*(100-tsd->near_attack_def_rate)/100;
		else	// BF_LONG (there's no other choice)
			cardfix=cardfix*(100-tsd->long_attack_def_rate)/100;

		if (cardfix != 1000)
			ATK_RATE(cardfix/10);
	}

	//SC_data fixes
	if (t_sc_data)
	{
		short scfix=1000;

		if(t_sc_data[SC_DEFENDER].timer != -1 && wd.flag&BF_LONG)
			scfix=scfix*(100-t_sc_data[SC_DEFENDER].val2)/100;
		
		if(t_sc_data[SC_FOGWALL].timer != -1 && wd.flag&BF_LONG)
			scfix=scfix/2;
		
		if(t_sc_data[SC_ASSUMPTIO].timer != -1){
			if(!map[target->m].flag.pvp)
				scfix=scfix*2/3;
			else
				scfix=scfix/2;
		}
	
		if(scfix != 1000)
			ATK_RATE(scfix/10);
   }

	if(t_mode&0x40)
	{ //Plants receive 1 damage when hit
		if (flag.rh && (flag.hit || wd.damage>0))
			wd.damage = 1;
		if (flag.lh && (flag.hit || wd.damage2>0))
			wd.damage2 = 1;
		return wd;
	}
	
	if(!flag.rh || wd.damage<1)
		wd.damage=0;
	
	if(!flag.lh || wd.damage2<1)
		wd.damage2=0;

	//Double is basicly a normal attack x2, so... [Skotlex]
	if (skill_num == TF_DOUBLE)
		wd.damage *=2;
	
	if (sd)
	{
		if (!flag.rh && flag.lh) 
		{	//Move lh damage to the rh
			wd.damage = wd.damage2;
			wd.damage2 = 0;
			flag.rh=1;
			flag.lh=0;
		}
		else if(sd->status.weapon > 16)
		{	//Dual-wield
			if (wd.damage > 0)
			{
				skill = pc_checkskill(*sd,AS_RIGHT);
				wd.damage = wd.damage * (50 + (skill * 10))/100;
				if(wd.damage < 1) wd.damage = 1;
			}
			if (wd.damage2 > 0)
			{
				skill = pc_checkskill(*sd,AS_LEFT);
				wd.damage2 = wd.damage2 * (30 + (skill * 10))/100;
				if(wd.damage2 < 1) wd.damage2 = 1;
			}
		}
		else if(sd->status.weapon == 16)
		{ //Katars
			skill = pc_checkskill(*sd,TF_DOUBLE);
			wd.damage2 = wd.damage * (1 + (skill * 2))/100;

			if(wd.damage > 0 && wd.damage2 < 1) wd.damage2 = 1;
			flag.lh = 1;
		}
	}

	if(skill_num != CR_GRANDCROSS && (wd.damage > 0 || wd.damage2 > 0) )
	{
		if(wd.damage2<1)
			wd.damage=battle_calc_damage(src,target,wd.damage,wd.div_,skill_num,skill_lv,wd.flag);
		else if(wd.damage<1)
			wd.damage2=battle_calc_damage(src,target,wd.damage2,wd.div_,skill_num,skill_lv,wd.flag);
		else
		{
			int d1=wd.damage+wd.damage2,d2=wd.damage2;
			wd.damage=battle_calc_damage(src,target,d1,wd.div_,skill_num,skill_lv,wd.flag);
			wd.damage2=(d2*100/d1)*wd.damage/100;
			if(wd.damage > 1 && wd.damage2 < 1) wd.damage2=1;
			wd.damage-=wd.damage2;
		}
	}

	if(sd && sd->classchange && tmd && (rand()%10000 < sd->classchange))
	{	//Classchange:
		static int changeclass[]={
			1001,1002,1004,1005,1007,1008,1009,1010,1011,1012,1013,1014,1015,1016,1018,1019,1020,
			1021,1023,1024,1025,1026,1028,1029,1030,1031,1032,1033,1034,1035,1036,1037,1040,1041,
			1042,1044,1045,1047,1048,1049,1050,1051,1052,1053,1054,1055,1056,1057,1058,1060,1061,
			1062,1063,1064,1065,1066,1067,1068,1069,1070,1071,1076,1077,1078,1079,1080,1081,1083,
			1084,1085,1094,1095,1097,1099,1100,1101,1102,1103,1104,1105,1106,1107,1108,1109,1110,
			1111,1113,1114,1116,1117,1118,1119,1121,1122,1123,1124,1125,1126,1127,1128,1129,1130,
			1131,1132,1133,1134,1135,1138,1139,1140,1141,1142,1143,1144,1145,1146,1148,1149,1151,
			1152,1153,1154,1155,1156,1158,1160,1161,1163,1164,1165,1166,1167,1169,1170,1174,1175,
			1176,1177,1178,1179,1180,1182,1183,1184,1185,1188,1189,1191,1192,1193,1194,1195,1196,
			1197,1199,1200,1201,1202,1204,1205,1206,1207,1208,1209,1211,1212,1213,1214,1215,1216,
			1219,1242,1243,1245,1246,1247,1248,1249,1250,1253,1254,1255,1256,1257,1258,1260,1261,
			1263,1264,1265,1266,1267,1269,1270,1271,1273,1274,1275,1276,1277,1278,1280,1281,1282,
			1291,1292,1293,1294,1295,1297,1298,1300,1301,1302,1304,1305,1306,1308,1309,1310,1311,
			1313,1314,1315,1316,1317,1318,1319,1320,1321,1322,1323,1364,1365,1366,1367,1368,1369,
			1370,1371,1372,1374,1375,1376,1377,1378,1379,1380,1381,1382,1383,1384,1385,1386,1387,
			1390,1391,1392,1400,1401,1402,1403,1404,1405,1406,1408,1409,1410,1412,1413,1415,1416,
			1417,1493,1494,1495,1497,1498,1499,1500,1502,1503,1504,1505,1506,1507,1508,1509,1510,
			1511,1512,1513,1514,1515,1516,1517,1519,1520,1582,1584,1585,1586,1587 };
		mob_class_change(*tmd, changeclass, sizeof(changeclass)/sizeof(changeclass[0]));
	}
	return wd;
}
/*==========================================
 * ����_���[�W�v�Z
 *------------------------------------------
 */
struct Damage battle_calc_weapon_attack(struct block_list *src,struct block_list *target,int skill_num,int skill_lv,int wflag)
{
	struct Damage wd;

	//return�O�̏���������̂ŏ��o�͕��̂ݕύX
	if (src == NULL || target == NULL  || (src->m != target->m) )
	{
		nullpo_info(NLP_MARK);
		memset(&wd,0,sizeof(wd));
		return wd;
	}

	//Until the function becomes official [Skotlex]
	if (battle_config.new_attack_function)
		wd = battle_calc_weapon_attack_sub(src,target,skill_num,skill_lv,wflag);
	else
	{
		if(target->type == BL_PET)
			memset(&wd,0,sizeof(wd));
		else if(src->type == BL_PC)
			wd = battle_calc_pc_weapon_attack(src,target,skill_num,skill_lv,wflag);
		else if(src->type == BL_MOB)
			wd = battle_calc_mob_weapon_attack(src,target,skill_num,skill_lv,wflag);
		else if(src->type == BL_PET)
			wd = battle_calc_pet_weapon_attack(src,target,skill_num,skill_lv,wflag);
		else
			memset(&wd,0,sizeof(wd));
	}

	if( src->type==BL_PC && (wd.damage > 0 || wd.damage2 > 0) &&
		( battle_config.equip_self_break_rate || battle_config.equip_skill_break_rate ) ) {
		struct map_session_data *sd = (struct map_session_data *)src;

		if (battle_config.equip_self_break_rate && sd->status.weapon != 11)
		{	//Self weapon breaking chance (Bows exempted)
			int breakrate = battle_config.equip_natural_break_rate;	//default self weapon breaking chance [DracoRPG]
				if(sd->sc_data[SC_OVERTHRUST].timer!=-1)
					breakrate += 10;
				if(sd->sc_data[SC_MAXOVERTHRUST].timer!=-1)
					breakrate += 10;				

			if((size_t)rand() % 10000 < breakrate * battle_config.equip_self_break_rate / 100 || breakrate >= 10000)
				if (pc_breakweapon(*sd))
				{
					if (battle_config.new_attack_function)
						wd = battle_calc_weapon_attack_sub(src,target,skill_num,skill_lv,wflag);
					else
						wd = battle_calc_pc_weapon_attack(src,target,skill_num,skill_lv,wflag);
				}
		}
		if (battle_config.equip_skill_break_rate)
		{	//Target equipment breaking
			// weapon = 0, armor = 1
			int breakrate_[2] = {0,0};	//target breaking chance [celest]
			int breaktime = 5000;

			breakrate_[0] += sd->break_weapon_rate;
			breakrate_[1] += sd->break_armor_rate;

				if (sd->sc_data[SC_MELTDOWN].timer!=-1) {
					breakrate_[0] += 100*sd->sc_data[SC_MELTDOWN].val1;
					breakrate_[1] = 70*sd->sc_data[SC_MELTDOWN].val1;
					breaktime = skill_get_time2(WS_MELTDOWN,1);
				}
			
			if((size_t)rand() % 10000 < breakrate_[0] * battle_config.equip_skill_break_rate / 100 || breakrate_[0] >= 10000) {
				if (target->type == BL_PC) {
					struct map_session_data *tsd = (struct map_session_data *)target;
					if(tsd->status.weapon != 11)
						pc_breakweapon(*tsd);
				} else
					status_change_start(target,SC_STRIPWEAPON,1,75,0,0,breaktime,0);
			}
			if((size_t)rand() % 10000 < breakrate_[1] * battle_config.equip_skill_break_rate/100 || breakrate_[1] >= 10000) {
				if (target->type == BL_PC) {
					struct map_session_data *tsd = (struct map_session_data *)target;
					pc_breakarmor(*tsd);
				} else
					status_change_start(target,SC_STRIPSHIELD,1,75,0,0,breaktime,0);
			}
		}
	}
	return wd;
}

/*==========================================
 * ���@�_���[�W�v�Z
 *------------------------------------------
 */
struct Damage battle_calc_magic_attack(struct block_list *bl,struct block_list *target,int skill_num,int skill_lv,int flag)
	{
	int mdef1, mdef2, matk1, matk2, damage = 0, div_ = 1, blewcount, rdamage = 0;
	int ele=0, race=7, size=1, race2=7, t_ele=0, t_race=7, t_mode = 0, cardfix, t_class, i;
	struct map_session_data *sd = NULL, *tsd = NULL;
	struct mob_data *tmd = NULL;
	struct Damage md;
	int aflag;	
	int normalmagic_flag = 1;
	int matk_flag = 1;
	int no_cardfix = 0;
	int no_elefix = 0;

	//return�O�̏���������̂ŏ��o�͕��̂ݕύX
	if( bl == NULL || target == NULL ){
		nullpo_info(NLP_MARK);
		memset(&md,0,sizeof(md));
		return md;
	}

	if(target->type == BL_PET) {
		memset(&md,0,sizeof(md));
		return md;
	}

	blewcount = skill_get_blewcount(skill_num,skill_lv);
	matk1=status_get_matk1(bl);
	matk2=status_get_matk2(bl);
	ele = skill_get_pl(skill_num);
	race = status_get_race(bl);
	size = status_get_size(bl);
	race2 = status_get_race2(bl);
	mdef1 = status_get_mdef(target);
	mdef2 = status_get_mdef2(target);
	t_ele = status_get_elem_type(target);
	t_race = status_get_race(target);
	t_mode = status_get_mode(target);

#define MATK_FIX( a,b ) { matk1=matk1*(a)/(b); matk2=matk2*(a)/(b); }

	if( bl->type==BL_PC && (sd=(struct map_session_data *)bl) ){
		sd->state.attack_type = BF_MAGIC;
		if(sd->matk_rate != 100)
			MATK_FIX(sd->matk_rate,100);
		sd->state.arrow_atk = 0;
	}
	if( target->type==BL_PC )
		tsd=(struct map_session_data *)target;
	else if( target->type==BL_MOB )
		tmd=(struct mob_data *)target;

	aflag=BF_MAGIC|BF_LONG|BF_SKILL;

	if(skill_num > 0){
		switch(skill_num){	// ��{�_���[�W�v�Z(�X�L�����Ƃɏ���)
					// �q�[��or����
		case AL_HEAL:
		case PR_BENEDICTIO:
			damage = skill_calc_heal(bl,skill_lv)/2;
			normalmagic_flag=0;
			break;
		case PR_ASPERSIO:		/* �A�X�y���V�I */
			damage = 40; //�Œ�_���[�W
			normalmagic_flag=0;
			break;
		case PR_SANCTUARY:	// �T���N�`���A��
			damage = (skill_lv>6)?388:skill_lv*50;
			normalmagic_flag=0;
			blewcount|=0x10000;
			break;
		case ALL_RESURRECTION:
		case PR_TURNUNDEAD:	// �U�����U���N�V�����ƃ^�[���A���f�b�h
			if(target->type != BL_PC && battle_check_undead(t_race,t_ele)){
				int hp, mhp, thres;
				hp = status_get_hp(target);
				mhp = status_get_max_hp(target);
				thres = (skill_lv * 20) + status_get_luk(bl)+
						status_get_int(bl) + status_get_lv(bl)+
						((200 - hp * 200 / mhp));
				if(thres > 700) thres = 700;
//				if(battle_config.battle_log)
//					ShowMessage("�^�[���A���f�b�h�I �m��%d ��(�番��)\n", thres);
				if(rand()%1000 < thres && !(t_mode&0x20))	// ����
					damage = hp;
				else					// ���s
					damage = status_get_lv(bl) + status_get_int(bl) + skill_lv * 10;
			}
			normalmagic_flag=0;
			break;

		case MG_NAPALMBEAT:	// �i�p�[���r�[�g�i���U�v�Z���݁j
			MATK_FIX(70+ skill_lv*10,100);
			if(flag>0){
				MATK_FIX(1,flag);
			}else {
				if(battle_config.error_log)
					ShowMessage("battle_calc_magic_attack(): napam enemy count=0 !\n");
			}
			break;

		case MG_SOULSTRIKE:			/* �\�E���X�g���C�N �i�΃A���f�b�h�_���[�W�␳�j*/
			if (battle_check_undead(t_race,t_ele)) {
				matk1 += matk1*skill_lv/20;//MATK�ɕ␳����ʖڂł����ˁH
				matk2 += matk2*skill_lv/20;
			}
			break;

		case MG_FIREBALL:	// �t�@�C���[�{�[��
			{
				const int drate[]={100,90,70};
				if(flag>2)
					matk1=matk2=0;
				else
					MATK_FIX( (95+skill_lv*5)*drate[flag] ,10000 );
			}
			break;

		case MG_FIREWALL:	// �t�@�C���[�E�H�[��
/*
			if( (t_ele!=3 && !battle_check_undead(t_race,t_ele)) || target->type==BL_PC ) //PC�͉Α����ł���ԁH���������_���[�W�󂯂�H
				blewcount |= 0x10000;
			else
				blewcount = 0;
*/
			if((t_ele==3 || battle_check_undead(t_race,t_ele)) && target->type!=BL_PC)
				blewcount = 0;
			else
				blewcount |= 0x10000;
			MATK_FIX( 1,2 );
			break;
		case MG_THUNDERSTORM:	// �T���_�[�X�g�[��
			MATK_FIX( 80,100 );
			break;
		case MG_FROSTDIVER:	// �t���X�g�_�C�o
			MATK_FIX( 100+skill_lv*10, 100);
			break;
		case WZ_FROSTNOVA:	// �t���X�g�_�C�o
			MATK_FIX((100+skill_lv*10)*2/3, 100);
			break;
		case WZ_FIREPILLAR:	// �t�@�C���[�s���[
			if(mdef1 < 1000000)
				mdef1=mdef2=0;	// MDEF����
			MATK_FIX( 1,5 );
			matk1+=50;
			matk2+=50;
			break;
		case WZ_SIGHTRASHER:
			MATK_FIX( 100+skill_lv*20, 100);
			break;
		case WZ_METEOR:
		case WZ_JUPITEL:	// ���s�e���T���_�[
			break;
		case WZ_VERMILION:	// ���[�h�I�u�o�[�~���I��
			MATK_FIX( skill_lv*20+80, 100 );
			break;
		case WZ_WATERBALL:	// �E�H�[�^�[�{�[��
			MATK_FIX( 100+skill_lv*30, 100 );
			break;
		case WZ_STORMGUST:	// �X�g�[���K�X�g
			MATK_FIX( skill_lv*40+100 ,100 );
//			blewcount|=0x10000;
			break;
		case AL_HOLYLIGHT:	// �z�[���[���C�g
			MATK_FIX( 125,100 );
			break;
		case AL_RUWACH:
			MATK_FIX( 145,100 );
			break;
		case HW_NAPALMVULCAN:	// �i�p�[���r�[�g�i���U�v�Z���݁j
			MATK_FIX(70+ skill_lv*10,100);
			if(flag>0){
				MATK_FIX(1,flag);
			}else {
				if(battle_config.error_log)
					ShowMessage("battle_calc_magic_attack(): napalmvulcan enemy count=0 !\n");
			}
			break;
		case PF_SOULBURN: // Celest
			if (target->type != BL_PC || skill_lv < 5) {
				memset(&md,0,sizeof(md));
				return md;
			} else if (target->type == BL_PC) {
				damage = ((struct map_session_data *)target)->status.sp * 2;
				matk_flag = 0; // don't consider matk and matk2
			}
			break;
		case ASC_BREAKER:
			damage = rand()%500 + 500 + skill_lv * status_get_int(bl) * 5;
			matk_flag = 0; // don't consider matk and matk2
			break;
		case HW_GRAVITATION:
			damage = 200 + skill_lv * 200;
			normalmagic_flag = 0;
			no_cardfix = 1;
			no_elefix = 1;
			break;
		}
	}

	if(normalmagic_flag){	// ��ʖ��@�_���[�W�v�Z
		int imdef_flag=0;
		if (matk_flag) {
			if(matk1>matk2)
				damage= matk2+rand()%(matk1-matk2+1);
			else
				damage= matk2;
		}
		if(sd) {
			if(sd->ignore_mdef_ele & (1<<t_ele) || sd->ignore_mdef_race & (1<<t_race))
				imdef_flag = 1;
			if(is_boss(target)) {
				if(sd->ignore_mdef_race & (1<<10))
					imdef_flag = 1;
			}
			else {
				if(sd->ignore_mdef_race & (1<<11))
					imdef_flag = 1;
			}
		}
		if(!imdef_flag){
			if(battle_config.magic_defense_type) {
				damage = damage - (mdef1 * battle_config.magic_defense_type) - mdef2;
			}
			else{
			damage = (damage*(100-mdef1))/100 - mdef2;
			}
		}

		if(damage<1)
			damage=1;
	}

	if (sd && !no_cardfix) {
		cardfix=100;
		cardfix=cardfix*(100+sd->magic_addrace[t_race])/100;
		cardfix=cardfix*(100+sd->magic_addele[t_ele])/100;
		if(is_boss(target))
			cardfix=cardfix*(100+sd->magic_addrace[10])/100;
		else
			cardfix=cardfix*(100+sd->magic_addrace[11])/100;
		t_class = status_get_class(target);
		for(i=0;i<sd->add_magic_damage_class_count;i++) {
			if(sd->add_magic_damage_classid[i] == t_class) {
				cardfix=cardfix*(100+sd->add_magic_damage_classrate[i])/100;
				break;
			}
		}
		damage=damage*cardfix/100;
		if (skill_num > 0 && sd->skillatk[0] == skill_num)
			damage += damage*sd->skillatk[1]/100;
	}

	if (tsd && !no_cardfix) {
		int s_class = status_get_class(bl);
		cardfix=100;
		cardfix=cardfix*(100-tsd->subele[ele])/100;	// �� ���ɂ��_���[�W�ϐ�
		cardfix=cardfix*(100-tsd->subrace[race])/100;	// �푰�ɂ��_���[�W�ϐ�
		cardfix=cardfix*(100-tsd->subsize[size])/100;
		cardfix=cardfix*(100-tsd->magic_subrace[race])/100;
		cardfix=cardfix*(100-tsd->subrace2[race2])/100;	// �푰�ɂ��_���[�W�ϐ�
		if(is_boss(bl))
			cardfix=cardfix*(100-tsd->magic_subrace[10])/100;
		else
			cardfix=cardfix*(100-tsd->magic_subrace[11])/100;
		for(i=0;i<tsd->add_mdef_class_count;i++) {
			if(tsd->add_mdef_classid[i] == s_class) {
				cardfix=cardfix*(100-tsd->add_mdef_classrate[i])/100;
				break;
			}
		}
		cardfix=cardfix*(100-tsd->magic_def_rate)/100;
		damage=damage*cardfix/100;
	}
	if(damage < 0) damage = 0;

	if (!no_elefix)
		damage=battle_attr_fix(damage, ele, status_get_element(target) );		// �� ���C��

	if(skill_num == CR_GRANDCROSS) {	// �O�����h�N���X
		struct Damage wd;
		wd=battle_calc_weapon_attack(bl,target,skill_num,skill_lv,flag);
		damage = (damage + wd.damage) * (100 + 40*skill_lv)/100;
		if(battle_config.gx_dupele) damage=battle_attr_fix(damage, ele, status_get_element(target) );	//����2�񂩂���
		if(bl==target){
			if(bl->type == BL_MOB)
				damage = 0;		//MOB���g���ꍇ�͔�������
			else
				damage=damage/2;	//�����͔���
		}
	}

	div_=skill_get_num( skill_num,skill_lv );

	if(div_>1 && skill_num != WZ_VERMILION)
		damage*=div_;

//	if(mdef1 >= 1000000 && damage > 0)
	if(t_mode&0x40 && damage > 0)
		damage = 1;

	if(is_boss(target))
		blewcount = 0;

	if (tsd && status_isimmune(target)) {
		if (sd && battle_config.gtb_pvp_only != 0)  { // [MouseJstr]
			damage = (damage * (100 - battle_config.gtb_pvp_only)) / 100;
		} else damage = 0;	// �� ��峃J�[�h�i���@�_���[�W�O�j
	}

	damage=battle_calc_damage(bl,target,damage,div_,skill_num,skill_lv,aflag);	// �ŏI�C��

	/* magic_damage_return by [AppleGirl] and [Valaris]		*/
	if( target->type==BL_PC && tsd && tsd->magic_damage_return > 0 ){
		rdamage += damage * tsd->magic_damage_return / 100;
			if(rdamage < 1) rdamage = 1;
			clif_damage(*target,*bl,gettick(),0,0,rdamage,0,0,0);
			battle_damage(target,bl,rdamage,0);
	}
	/*			end magic_damage_return			*/

	md.damage=damage;
	md.div_=div_;
	md.amotion=status_get_amotion(bl);
	md.dmotion=status_get_dmotion(target);
	md.damage2=0;
	md.type=0;
	md.blewcount=blewcount;
	md.flag=aflag;

	return md;
}

/*==========================================
 * ���̑��_���[�W�v�Z
 *------------------------------------------
 */
struct Damage  battle_calc_misc_attack(struct block_list *bl,struct block_list *target,int skill_num,int skill_lv,int flag)
{
	int int_=status_get_int(bl);
//	int luk=status_get_luk(bl);
	int dex=status_get_dex(bl);
	int skill,ele,race,size,cardfix,race2,t_mode;
	struct map_session_data *sd=NULL,*tsd=NULL;
	int damage=0,div_=1,blewcount=skill_get_blewcount(skill_num,skill_lv);
	struct Damage md;
	int damagefix=1;
	int self_damage=0;
	int aflag=BF_MISC|BF_SHORT|BF_SKILL;

	//return�O�̏���������̂ŏ��o�͕��̂ݕύX
	if( bl == NULL || target == NULL ){
		nullpo_info(NLP_MARK);
		memset(&md,0,sizeof(md));
		return md;
	}

	if(target->type == BL_PET) {
		memset(&md,0,sizeof(md));
		return md;
	}

	if( bl->type == BL_PC && (sd=(struct map_session_data *)bl) ) {
		sd->state.attack_type = BF_MISC;
		sd->state.arrow_atk = 0;
	}

	if( target->type==BL_PC )
		tsd=(struct map_session_data *)target;

	t_mode = status_get_mode(target);
	ele = skill_get_pl(skill_num);
	race = status_get_race(bl);
	size = status_get_size(bl);
	race2 = status_get_race(bl);

	switch(skill_num){

	case HT_LANDMINE:	// �����h�}�C��
		damage=skill_lv*(dex+75)*(100+int_)/100;
		break;

	case HT_BLASTMINE:	// �u���X�g�}�C��
		damage=skill_lv*(dex/2+50)*(100+int_)/100;
		break;

	case HT_CLAYMORETRAP:	// �N���C���A�[�g���b�v
		damage=skill_lv*(dex/2+75)*(100+int_)/100;
		break;

	case HT_BLITZBEAT:	// �u���b�c�r�[�g
		if( sd==NULL || (skill = pc_checkskill(*sd,HT_STEELCROW)) <= 0)
			skill=0;
		damage=(dex/10+int_/2+skill*3+40)*2;
		if(flag > 1)
			damage /= flag;
		aflag |= (flag&~BF_RANGEMASK)|BF_LONG;
		break;

	case TF_THROWSTONE:	// �Γ���
		damage=50;
		damagefix=0;
		aflag |= (flag&~BF_RANGEMASK)|BF_LONG;
		break;

	case BA_DISSONANCE:	// �s���a��
		if(sd)
		damage=30+(skill_lv)*10+pc_checkskill(*sd,BA_MUSICALLESSON)*3;
		break;

	case NPC_SELFDESTRUCTION:	// ����
		damage = status_get_hp(bl);
		damagefix = 0;
		break;

	case NPC_SMOKING:	// �^�o�R���z��
		damage=3;
		damagefix=0;
		break;

	case NPC_DARKBREATH:
		{
			struct status_change *sc_data = status_get_sc_data(target);
			int hitrate=status_get_hit(bl) - status_get_flee(target) + 80;
			hitrate = ( (hitrate>95)?95: ((hitrate<5)?5:hitrate) );
			if(sc_data && (sc_data[SC_SLEEP].timer!=-1 || sc_data[SC_STAN].timer!=-1 ||
				sc_data[SC_FREEZE].timer!=-1 || (sc_data[SC_STONE].timer!=-1 && sc_data[SC_STONE].val2==0) ) )
				hitrate = 1000000;
			if(rand()%100 < hitrate) {
				damage = 500 + (skill_lv-1)*1000 + rand()%1000;
				if(damage > 9999) damage = 9999;
			}
		}
		break;
	case SN_FALCONASSAULT:			/* �t�@���R���A�T���g */
	{
#ifdef TWILIGHT
		if( sd==NULL || (skill = pc_checkskill(*sd,HT_BLITZBEAT)) <= 0)
			skill=0;
 		damage=(100+70*skill_lv+(dex/10+int_/2+skill*3+40)*2) * 2;
#else
		if( sd==NULL || (skill = pc_checkskill(*sd,HT_STEELCROW)) <= 0)
			skill=0;
		damage=((150+70*skill_lv)*(dex/10+int_/2+skill*3+40)*2)/100; // [Celest]
#endif
		if(flag > 1)
			damage /= flag;
		aflag |= (flag&~BF_RANGEMASK)|BF_LONG;
		break;
	}
	case PA_SACRIFICE:
	{	
		if( status_get_mexp(target) )
			self_damage = 1;
		else
			self_damage = status_get_max_hp(bl)*9/100;
		ele = status_get_attack_element(bl);
		damage = self_damage + (self_damage/10)*(skill_lv-1);
		break;
	}
	}//end case

	if(damagefix){
		if(damage<1 && skill_num != NPC_DARKBREATH)
			damage=1;

		if( tsd ){
			cardfix=100;
			cardfix=cardfix*(100-tsd->subele[ele])/100;	// �����ɂ��_���[�W�ϐ�
			cardfix=cardfix*(100-tsd->subrace[race])/100;	// �푰�ɂ��_���[�W�ϐ�
			cardfix=cardfix*(100-tsd->subsize[size])/100;
			cardfix=cardfix*(100-tsd->misc_def_rate)/100;
			cardfix=cardfix*(100-tsd->subrace2[race2])/100;
			damage=damage*cardfix/100;
		}
		if (sd && skill_num > 0 && sd->skillatk[0] == skill_num)
			damage += damage*sd->skillatk[1]/100;

		if(damage < 0) damage = 0;
		damage=battle_attr_fix(damage, ele, status_get_element(target) );		// �����C��
	}

	div_=skill_get_num( skill_num,skill_lv );
	if(div_>1)
		damage*=div_;

	if(damage > 0 && (damage < div_ || (status_get_def(target) >= 1000000 && status_get_mdef(target) >= 1000000) ) ) {
		damage = div_;
	}

	if(t_mode&0x40 && damage>0)
		damage = 1;

	if(is_boss(target))
		blewcount = 0;

	if(self_damage)
	{
		if(sd)
			pc_damage(*sd, self_damage, bl);
		clif_damage(*bl,*bl, gettick(), 0, 0, self_damage, 0 , 0, 0);
	}

	damage=battle_calc_damage(bl,target,damage,div_,skill_num,skill_lv,aflag);	// �ŏI�C��

	md.damage=damage;
	md.div_=div_;
	md.amotion=status_get_amotion(bl);
	md.dmotion=status_get_dmotion(target);
	md.damage2=0;
	md.type=0;
	md.blewcount=blewcount;
	md.flag=aflag;
	return md;

}
/*==========================================
 * �_���[�W�v�Z�ꊇ�����p
 *------------------------------------------
 */
struct Damage battle_calc_attack(int attack_type, struct block_list *bl,struct block_list *target, int skill_num,int skill_lv,int flag)
{
	struct Damage d;
	switch(attack_type){
	case BF_WEAPON:
		return battle_calc_weapon_attack(bl,target,skill_num,skill_lv,flag);
	case BF_MAGIC:
		return battle_calc_magic_attack(bl,target,skill_num,skill_lv,flag);
	case BF_MISC:
		return battle_calc_misc_attack(bl,target,skill_num,skill_lv,flag);
	default:
		if (battle_config.error_log)
			ShowError("battle_calc_attack: unknown attack type! %d\n",attack_type);
		memset(&d,0,sizeof(d));
		break;
	}
	return d;
}
/*==========================================
 * �ʏ�U�������܂Ƃ�
 *------------------------------------------
 */
int battle_weapon_attack(struct block_list *src, struct block_list *target, unsigned long tick, int flag)
{
	struct map_session_data *sd = NULL;
	struct map_session_data *tsd = NULL;
	struct status_change *sc_data;
	struct status_change *tsc_data;
	int race, ele, damage, rdamage = 0;
	struct Damage wd = {0,0,0,0,0,0,0,0,0};
	short *opt1;

	nullpo_retr(0, src);
	nullpo_retr(0, target);

	if (src->prev == NULL || target->prev == NULL)
		return 0;

	if(src->type == BL_PC)
	{
		sd = (struct map_session_data *)src;
		if( pc_isdead(*sd) )
		return 0;
	}

	if (target->type == BL_PC)
	{
		tsd = (struct map_session_data *)target;
		if( pc_isdead(*tsd) )
		return 0;
	}

	opt1 = status_get_opt1(src);
	if (opt1 && *opt1 > 0) {
		battle_stopattack(src);
		return 0;
	}

	sc_data = status_get_sc_data(src);
	tsc_data = status_get_sc_data(target);

	if (sc_data && sc_data[SC_BLADESTOP].timer != -1) {
		battle_stopattack(src);
		return 0;
	}

	if (battle_check_target(src,target,BCT_ENEMY) <= 0 && !battle_check_range(src,target,0))
		return 0;	// �U���ΏۊO

	race = status_get_race(target);
	ele = status_get_elem_type(target);
	if( battle_check_target(src,target,BCT_ENEMY) > 0 && battle_check_range(src,target,0) )
	{	// �U���ΏۂƂȂ肤��̂ōU��
		if(sd && sd->status.weapon == 11)
		{
			if(sd->equip_index[10] < MAX_INVENTORY)
			{
				if(battle_config.arrow_decrement)
					pc_delitem(*sd,sd->equip_index[10],1,0);
			}
			else
			{
				clif_arrow_fail(*sd,0);
				return 0;
			}
		}
		if(flag&0x8000)
		{
			if(sd && battle_config.pc_attack_direction_change)
				sd->dir = sd->head_dir = map_calc_dir(*src, target->x,target->y );
			else if(src->type == BL_MOB && battle_config.monster_attack_direction_change)
			{
				struct mob_data *md = (struct mob_data *)src;
				if (md) md->dir = map_calc_dir(*src, target->x, target->y);
			}
			wd = battle_calc_weapon_attack(src, target, KN_AUTOCOUNTER, flag&0xff, 0);
		}
		else if (flag & AS_POISONREACT && sc_data && sc_data[SC_POISONREACT].timer != -1)
			wd = battle_calc_weapon_attack(src, target, AS_POISONREACT, sc_data[SC_POISONREACT].val1, 0);
		else
			wd = battle_calc_weapon_attack(src,target,0,0,0);
	
		if((damage = wd.damage + wd.damage2) > 0 && src != target)
		{
			if(wd.flag & BF_SHORT)
			{
				if(tsd && tsd->short_weapon_damage_return > 0)
				{
					rdamage += damage * tsd->short_weapon_damage_return / 100;
					if (rdamage < 1) rdamage = 1;
				}
				if(tsc_data && tsc_data[SC_REFLECTSHIELD].timer != -1)
				{
					rdamage += damage * tsc_data[SC_REFLECTSHIELD].val2 / 100;
					if (rdamage < 1) rdamage = 1;
				}
			}
			else if(wd.flag & BF_LONG)
			{
				if(tsd && tsd->long_weapon_damage_return > 0)
				{
					rdamage += damage * tsd->long_weapon_damage_return / 100;
					if(rdamage < 1) rdamage = 1;
				}
			}
			if (rdamage > 0)
				clif_damage(*src, *src, tick, wd.amotion, wd.dmotion, rdamage, 1, 4, 0);
		}

		if(wd.div_ == 255)
		{	//�O�i��
			int delay = 0;
			wd.div_ = 3;
			if(sd && wd.damage+wd.damage2 < status_get_hp(target))
			{
				int skilllv = pc_checkskill(*sd, MO_CHAINCOMBO);
				if (skilllv > 0) {
					delay = 1000 - 4 * status_get_agi(src) - 2 *  status_get_dex(src);
					delay += 300 * battle_config.combo_delay_rate / 100; //�ǉ��f�B���C��conf�ɂ�蒲��
				}
				status_change_start(src, SC_COMBO, MO_TRIPLEATTACK, skilllv, 0, 0, delay, 0);
			}
			if(sd) sd->attackabletime = sd->canmove_tick = tick + delay;

			clif_combo_delay(*src, delay);
			clif_skill_damage(*src, *target, tick, wd.amotion, wd.dmotion, wd.damage, wd.div_,
				MO_TRIPLEATTACK, (sd)?pc_checkskill(*sd,MO_TRIPLEATTACK):1, -1);
		}
		else
		{	//�񓁗�����ƃJ�^�[���ǌ��̃~�X�\��(�������`)
			clif_damage(*src, *target, tick, wd.amotion, wd.dmotion, wd.damage, wd.div_ , wd.type, wd.damage2);
			if(sd && sd->status.weapon >= 16 && wd.damage2 == 0)
				clif_damage(*src, *target, tick+10, wd.amotion, wd.dmotion,0, 1, 0, 0);
		}
		if (sd && sd->splash_range > 0 && (wd.damage > 0 || wd.damage2 > 0))
			skill_castend_damage_id(src,target,0,0,tick,0);

		map_freeblock_lock();

		battle_delay_damage(tick+wd.amotion, *src, *target, (wd.damage+wd.damage2), 0);

		if(target->prev != NULL && (wd.damage > 0 || wd.damage2 > 0))
		{
			skill_additional_effect(src, target, 0, 0, BF_WEAPON, tick);
			if(sd)
			{
				int hp = status_get_max_hp(target);
				if (sd->weapon_coma_ele[ele] > 0 && rand()%10000 < sd->weapon_coma_ele[ele])
					battle_damage(src, target, hp, 1);
				if (sd->weapon_coma_race[race] > 0 && rand()%10000 < sd->weapon_coma_race[race])
					battle_damage(src, target, hp, 1);
				if (is_boss(target)) {
					if(sd->weapon_coma_race[10] > 0 && rand()%10000 < sd->weapon_coma_race[10])
						battle_damage(src, target, hp, 1);
				}
				else
				{
					if (sd->weapon_coma_race[11] > 0 && rand()%10000 < sd->weapon_coma_race[11])
						battle_damage(src, target, hp, 1);
				}
			}
		}
		if(sc_data && sc_data[SC_AUTOSPELL].timer != -1 && rand()%100 < sc_data[SC_AUTOSPELL].val4)
		{
			int sp = 0, f = 0;
			int skillid = sc_data[SC_AUTOSPELL].val2;
			int skilllv = sc_data[SC_AUTOSPELL].val3;

			int i = rand()%100;
			if (i >= 50) skilllv -= 2;
			else if (i >= 15) skilllv--;
			if (skilllv < 1) skilllv = 1;

			if (sd) sp = skill_get_sp(skillid,skilllv) * 2 / 3;

			if((sd && sd->status.sp >= sp) || !sd)
			{
				if ((i = skill_get_inf(skillid) == 2) || i == 32)
					f = skill_castend_pos2(src, target->x, target->y, skillid, skilllv, tick, flag);
				else {
					switch(skill_get_nk(skillid)) {
						case NK_NO_DAMAGE:/* �x���n */
							if((skillid == AL_HEAL || (skillid == ALL_RESURRECTION && !tsd)) && battle_check_undead(race,ele))
								f = skill_castend_damage_id(src, target, skillid, skilllv, tick, flag);
							else
								f = skill_castend_nodamage_id(src, target, skillid, skilllv, tick, flag);
							break;
						case NK_SPLASH_DAMAGE:
						default:
							f = skill_castend_damage_id(src, target, skillid, skilllv, tick, flag);
							break;
					}
				}
				if(sd && !f)
				{
					pc_heal(*sd, 0, -sp);
			}
		}
		}
		if(sd)
		{
			size_t i;
			for (i = 0; i < 10; i++)
			{
				if(sd->autospell_id[i] != 0)
				{
					struct block_list *tbl;
					int skillid = (sd->autospell_id[i] > 0) ? sd->autospell_id[i] : -sd->autospell_id[i];
					int skilllv = (sd->autospell_lv[i] > 0) ? sd->autospell_lv[i] : 1;
					int j, rate = (!sd->state.arrow_atk) ? sd->autospell_rate[i] : sd->autospell_rate[i] / 2;
					
					if (rand()%100 > rate)
						continue;
					if (sd->autospell_id[i] < 0)
						tbl = src;
					else
						tbl = target;
					
					if ((j = skill_get_inf(skillid) == 2) || j == 32)
						skill_castend_pos2(src, tbl->x, tbl->y, skillid, skilllv, tick, flag);
					else {
						switch (skill_get_nk(skillid)) {
							case NK_NO_DAMAGE:/* �x���n */
								if ((skillid == AL_HEAL || (skillid == ALL_RESURRECTION && tbl->type != BL_PC)) && battle_check_undead(race,ele))
									skill_castend_damage_id(src, tbl, skillid, skilllv, tick, flag);
								else
									skill_castend_nodamage_id(src, tbl, skillid, skilllv, tick, flag);
								break;
							case NK_SPLASH_DAMAGE:
							default:
								skill_castend_damage_id(src, tbl, skillid, skilllv, tick, flag);
								break;

						}
					}
				} else break;
			}
			if(wd.flag&BF_WEAPON && src != target && (wd.damage > 0 || wd.damage2 > 0))
			{
				int hp = 0, sp = 0;
				if(!battle_config.left_cardfix_to_right)
				{	// �񓁗�����J�[�h�̋z���n���ʂ��E��ɒǉ����Ȃ��ꍇ
					hp += battle_calc_drain(wd.damage, sd->right_weapon.hp_drain_rate, sd->right_weapon.hp_drain_per, sd->right_weapon.hp_drain_value);
					hp += battle_calc_drain(wd.damage2, sd->left_weapon.hp_drain_rate, sd->left_weapon.hp_drain_per, sd->left_weapon.hp_drain_value);
					sp += battle_calc_drain(wd.damage, sd->right_weapon.sp_drain_rate, sd->right_weapon.sp_drain_per, sd->right_weapon.sp_drain_value);
					sp += battle_calc_drain(wd.damage2, sd->left_weapon.sp_drain_rate, sd->left_weapon.sp_drain_per, sd->left_weapon.sp_drain_value);
				}
				else
				{	// �񓁗�����J�[�h�̋z���n���ʂ��E��ɒǉ�����ꍇ
					int hp_drain_rate = sd->right_weapon.hp_drain_rate + sd->left_weapon.hp_drain_rate;
					int hp_drain_per = sd->right_weapon.hp_drain_per + sd->left_weapon.hp_drain_per;
					int hp_drain_value = sd->right_weapon.hp_drain_value + sd->left_weapon.hp_drain_value;
					int sp_drain_rate = sd->right_weapon.sp_drain_rate + sd->left_weapon.sp_drain_rate;
					int sp_drain_per = sd->right_weapon.sp_drain_per + sd->left_weapon.sp_drain_per;
					int sp_drain_value = sd->right_weapon.sp_drain_value + sd->left_weapon.sp_drain_value;
					hp += battle_calc_drain(wd.damage, hp_drain_rate, hp_drain_per, hp_drain_value);
					sp += battle_calc_drain(wd.damage, sp_drain_rate, sp_drain_per, sp_drain_value);
				}

				if (battle_config.show_hp_sp_drain)
				{	//Display gained values [Skotlex]
					if (hp > 0 && pc_heal(*sd, hp, 0) > 0)
						clif_heal(sd->fd, SP_HP, hp);
					if (sp > 0 && pc_heal(*sd, 0, sp) > 0)
						clif_heal(sd->fd, SP_SP, sp);
				}
				else	if (hp || sp)
				{
					pc_heal(*sd, hp, sp);
				}

				if (tsd && sd->sp_drain_type)
					pc_heal(*tsd, 0, -sp);
			}
		}
		if(tsd)
		{
			int i;
			for (i = 0; i < 10; i++)
			{
				if(tsd->autospell2_id[i] != 0)
				{
					struct block_list *tbl;
					int skillid = (tsd->autospell2_id[i] > 0) ? tsd->autospell2_id[i] : -tsd->autospell2_id[i];
					int skilllv = (tsd->autospell2_lv[i] > 0) ? tsd->autospell2_lv[i] : 1;
					int j, rate = ((sd && !sd->state.arrow_atk) || (status_get_range(src)<=2)) ?
						tsd->autospell2_rate[i] : tsd->autospell2_rate[i] / 2;
					
					if (rand()%100 > rate)
						continue;
					if (tsd->autospell2_id[i] < 0)
						tbl = target;
					else 
						tbl = src;
					if ((j = skill_get_inf(skillid) == 2) || j == 32)
						skill_castend_pos2(target, tbl->x, tbl->y, skillid, skilllv, tick, flag);
					else {
						switch (skill_get_nk(skillid)) {
							case NK_NO_DAMAGE:/* �x���n */
								if ((skillid == AL_HEAL || (skillid == ALL_RESURRECTION && tbl->type != BL_PC)) &&
									battle_check_undead(status_get_race(tbl), status_get_elem_type(tbl)))
									skill_castend_damage_id(target, tbl, skillid, skilllv, tick, flag);
								else
									skill_castend_nodamage_id(target, tbl, skillid, skilllv, tick, flag);
								break;
							case NK_SPLASH_DAMAGE:
							default:
								skill_castend_damage_id(target, tbl, skillid, skilllv, tick, flag);
								break;
						}
					}
				} else break;
			}
		}
		if (rdamage > 0)
			battle_delay_damage(tick+wd.amotion, *target, *src, rdamage, 0);

		if(tsc_data)
		{
			if(tsc_data[SC_AUTOCOUNTER].timer != -1 && tsc_data[SC_AUTOCOUNTER].val4 > 0)
			{
				if( (unsigned long)tsc_data[SC_AUTOCOUNTER].val3 == src->id )
					battle_weapon_attack(target, src, tick, 0x8000|tsc_data[SC_AUTOCOUNTER].val1);
				status_change_end(target,SC_AUTOCOUNTER,-1);
			}
			if(tsc_data[SC_POISONREACT].timer != -1 && tsc_data[SC_POISONREACT].val4 > 0 && (unsigned long)tsc_data[SC_POISONREACT].val3 == src->id)
			{   // poison react [Celest]
				if(status_get_elem_type(src) == 5)
				{
					tsc_data[SC_POISONREACT].val2 = 0;
					battle_weapon_attack(target, src, tick, flag|AS_POISONREACT);
				}
				else
				{
					skill_castend_damage_id(target, src, TF_POISON, 5, tick, flag);
					tsc_data[SC_POISONREACT].val2--;
				}
				if (tsc_data[SC_POISONREACT].val2 <= 0)
					status_change_end(target,SC_POISONREACT,-1);
			}
			if (tsc_data[SC_BLADESTOP_WAIT].timer != -1 && !is_boss(src)) { // �{�X�ɂ͖���
				int skilllv = tsc_data[SC_BLADESTOP_WAIT].val1;
				status_change_end(target, SC_BLADESTOP_WAIT, -1);
				status_change_start(src, SC_BLADESTOP, skilllv, 1, (int)src, (int)target, skill_get_time2(MO_BLADESTOP,skilllv), 0);
				status_change_start(target, SC_BLADESTOP, skilllv, 2, (int)target, (int)src, skill_get_time2(MO_BLADESTOP,skilllv), 0);
			}
			if (tsc_data[SC_SPLASHER].timer != -1)	//�������̂őΏۂ̃x�i���X�v���b�V���[��Ԃ�����
				status_change_end(target, SC_SPLASHER, -1);
		}

		map_freeblock_unlock();
	}
	return wd.dmg_lv;
}

bool battle_check_undead(int race,int element)
{
	if(battle_config.undead_detect_type == 0) {
		if(element == 9)
			return true;
	}
	else if(battle_config.undead_detect_type == 1) {
		if(race == 1)
			return true;
	}
	else {
		if(element == 9 || race == 1)
			return true;
	}
	return false;
}

/*==========================================
 * �G��������(1=�m��,0=�ے�,-1=�G���[)
 * flag&0xf0000 = 0x00000:�G����Ȃ�������iret:1���G�ł͂Ȃ��j
 *				= 0x10000:�p�[�e�B�[����iret:1=�p�[�e�B�[�����o)
 *				= 0x20000:�S��(ret:1=�G��������)
 *				= 0x40000:�G������(ret:1=�G)
 *				= 0x50000:�p�[�e�B�[����Ȃ�������(ret:1=�p�[�e�B�łȂ�)
 *------------------------------------------
 */
int battle_check_target( struct block_list *src, struct block_list *target,int flag)
{
	unsigned long s_p,s_g,t_p,t_g;
	struct block_list *ss=src;
	struct status_change *sc_data;
	struct status_change *tsc_data;
	struct map_session_data *srcsd = NULL;
	struct map_session_data *tsd = NULL;

	nullpo_retr(0, src);
	nullpo_retr(0, target);

	if (flag & BCT_ENEMY){	// ���]�t���O
		int ret = battle_check_target(src,target,flag&0x30000);
		if (ret != -1)
			return !ret;
		return -1;
	}

	if (flag & BCT_ALL){
		if (target->type == BL_MOB || target->type == BL_PC)
			return 1;
		else
			return -1;
	}

	if (src->type == BL_SKILL && target->type == BL_SKILL)	// �Ώۂ��X�L�����j�b�g�Ȃ疳�����m��
		return -1;

	if (target->type == BL_PET)
		return -1;

	if (src->type == BL_PC) {
		nullpo_retr(-1, srcsd = (struct map_session_data *)src);
	}
	if (target->type == BL_PC) {
		nullpo_retr(-1, tsd = (struct map_session_data *)target);
	}
	
	if(tsd && (tsd->invincible_timer != -1 || pc_isinvisible(*tsd)))
		return -1;

	// Celest
	sc_data = status_get_sc_data(src);
	tsc_data = status_get_sc_data(target);
	if ((sc_data && sc_data[SC_BASILICA].timer != -1) ||
		(tsc_data && tsc_data[SC_BASILICA].timer != -1))
		return -1;

	if(target->type == BL_SKILL)
	{
		struct skill_unit *tsu = (struct skill_unit *)target;
		if (tsu && tsu->group) {
			switch (tsu->group->unit_id)
			{
			case 0x8d:
			case 0x8f:
			case 0x98:
				return 0;
				break;
			}
		}
	}

	// �X�L�����j�b�g�̏ꍇ�A�e�����߂�
	if( src->type==BL_SKILL)
	{
		struct skill_unit *su = (struct skill_unit *)src;
		if(su && su->group)
		{
			int skillid, inf2;		
			skillid = su->group->skill_id;
			inf2 = skill_get_inf2(skillid);
			if ((ss = map_id2bl(su->group->src_id)) == NULL)
				return -1;
			if (ss->prev == NULL)
				return -1;
			if (inf2&0x80 &&
				(map[src->m].flag.pvp ||
				(skillid >= 115 && skillid <= 125 && map[src->m].flag.gvg)) &&
				!(target->type == BL_PC && pc_isinvisible(*tsd)))
					return 0;
			if (ss == target) {
				if (inf2&0x100)
					return 0;
				if (inf2&0x200)
					return -1;
			}
		}
	}
	
	if (src->type == BL_MOB) {
		struct mob_data *md = (struct mob_data *)src;
		nullpo_retr (-1, md);

		if (tsd) {
			if(md->class_ >= 1285 && md->class_ <= 1287){
				struct guild_castle *gc = guild_mapname2gc (map[target->m].mapname);
				if(gc && agit_flag==0)	// Guardians will not attack during non-woe time [Valaris]
					return 1;  // end addition [Valaris]
				if(gc && tsd->status.guild_id > 0) {
					struct guild *g=guild_search(tsd->status.guild_id);	// don't attack guild members [Valaris]
					if(g && g->guild_id == gc->guild_id)
						return 1;
					if(g && guild_isallied(*g,*gc))
						return 1;
				}
			}
			// option to have monsters ignore GMs [Valaris]
			if (battle_config.monsters_ignore_gm > 0 && pc_isGM(*tsd) >= battle_config.monsters_ignore_gm)
				return 1;
		}
		// Mob��master_id��������special_mob_ai�Ȃ�A����������߂�
		if (md->master_id > 0) {
			if (md->master_id == target->id)	// ��Ȃ�m��
				return 1;
			if (md->state.special_mob_ai){
				if (target->type == BL_MOB){	//special_mob_ai�őΏۂ�Mob
					struct mob_data *tmd = (struct mob_data *)target;
					if (tmd){
						if(tmd->master_id != md->master_id)	//�����傪�ꏏ�łȂ���Δے�
							return 0;
						else{	//�����傪�ꏏ�Ȃ̂ōm�肵�������ǎ����͔ے�
							if(md->state.special_mob_ai>2)
								return 0;
							else
								return 1;
						}
					}
				}
			}
			if((ss = map_id2bl(md->master_id)) == NULL)
				return -1;
		}
	}

	if (src == target || ss == target)	// �����Ȃ�m��
		return 1;

	if (src->prev == NULL ||	// ����ł�Ȃ�G���[
		(srcsd && pc_isdead(*srcsd)))
		return -1;

	if ((ss->type == BL_PC && target->type == BL_MOB) ||
		(ss->type == BL_MOB && target->type == BL_PC) )
		return 0;	// PCvsMOB�Ȃ�ے�

	if (ss->type == BL_PET && target->type == BL_MOB)
		return 0;

	s_p = status_get_party_id(ss);
	s_g = status_get_guild_id(ss);

	t_p = status_get_party_id(target);
	t_g = status_get_guild_id(target);

	if (flag & 0x10000) {
		if (s_p && t_p && s_p == t_p)	// �����p�[�e�B�Ȃ�m��i�����j
			return 1;
		else		// �p�[�e�B�����Ȃ瓯���p�[�e�B����Ȃ����_�Ŕے�
			return 0;
	}

	if (ss->type == BL_MOB && s_g > 0 && t_g > 0 && s_g == t_g )	// �����M���h/mob�N���X�Ȃ�m��i�����j
		return 1;

//ShowMessage("ss:%d src:%d target:%d flag:0x%x %d %d ",ss->id,src->id,target->id,flag,src->type,target->type);
//ShowMessage("p:%d %d g:%d %d\n",s_p,t_p,s_g,t_g);

	if (ss->type == BL_PC && target->type == BL_PC) { // ����PVP���[�h�Ȃ�ے�i�G�j
		struct map_session_data *ssd = (struct map_session_data *)ss;		
		struct skill_unit *su = NULL;
		if (src->type == BL_SKILL)
			su = (struct skill_unit *)src;
		if (map[ss->m].flag.pvp || pc_iskiller(*ssd, *tsd)) { // [MouseJstr]
			if(su && su->group->target_flag == BCT_NOENEMY)
				return 1;
			else if (battle_config.pk_mode &&
				(ssd->status.class_ == 0 || tsd->status.class_ == 0 ||
				ssd->status.base_level < battle_config.pk_min_level ||
				tsd->status.base_level < battle_config.pk_min_level))
				return 1; // prevent novice engagement in pk_mode [Valaris]
			else if (map[ss->m].flag.pvp_noparty && s_p > 0 && t_p > 0 && s_p == t_p)
				return 1;
			else if (map[ss->m].flag.pvp_noguild && s_g > 0 && t_g > 0 && s_g == t_g)
				return 1;
			return 0;
		}
		if (map[src->m].flag.gvg || map[src->m].flag.gvg_dungeon) {
			struct guild *g;
			if (su && su->group->target_flag == BCT_NOENEMY)
				return 1;
			if (s_g > 0 && s_g == t_g)
				return 1;
			if (map[src->m].flag.gvg_noparty && s_p > 0 && t_p > 0 && s_p == t_p)
				return 1;
			if ((g = guild_search(s_g))) {
				int i;
				for (i = 0; i < MAX_GUILDALLIANCE; i++) {
					if (g->alliance[i].guild_id > 0 && g->alliance[i].guild_id == t_g) {
						if (g->alliance[i].opposition)
							return 0;//�G�΃M���h�Ȃ疳�����ɓG
						else
							return 1;//�����M���h�Ȃ疳�����ɖ���
					}
				}
			}
			return 0;
		}
	}

	return 1;	// �Y�����Ȃ��̂Ŗ��֌W�l���i�܂��G����Ȃ��̂Ŗ����j
}
/*==========================================
 * �˒�����
 *------------------------------------------
 */
bool battle_check_range(struct block_list *src,struct block_list *bl,unsigned int range)
{
	unsigned int dx,dy, arange;

	nullpo_retr(0, src);
	nullpo_retr(0, bl);

	dx=abs((int)bl->x - (int)src->x);
	dy=abs((int)bl->y - (int)src->y);
	arange=((dx>dy)?dx:dy);

	if(src->m != bl->m)	// �Ⴄ�}�b�v
		return false;

	if( range>0 && range < arange )	// ��������
		return false;

	if( arange<2 )	// �����}�X���א�
		return true;

//	if(bl->type == BL_SKILL && ((struct skill_unit *)bl)->group->unit_id == 0x8d)
//		return true;

	// ��Q������
	return path_search_long(src->m,src->x,src->y,bl->x,bl->y);
}


static struct {
	const char *str;
	ulong *val;
} battle_data[] = {
	{ "agi_penalty_count",                 &battle_config.agi_penalty_count			},
	{ "agi_penalty_count_lv",              &battle_config.agi_penalty_count_lv		},
	{ "agi_penalty_num",                   &battle_config.agi_penalty_num			},
	{ "agi_penalty_type",                  &battle_config.agi_penalty_type			},
	{ "alchemist_summon_reward",           &battle_config.alchemist_summon_reward	},	// [Valaris]
	{ "allow_atcommand_when_mute",			&battle_config.allow_atcommand_when_mute}, // [celest]
	{ "any_warp_GM_min_level",             &battle_config.any_warp_GM_min_level	}, // added by [Yor]
	{ "area_size",                         &battle_config.area_size	}, // added by [MouseJstr]
	{ "arrow_decrement",                   &battle_config.arrow_decrement			},
	{ "atcommand_gm_only",                 &battle_config.atc_gmonly				},
	{ "atcommand_spawn_quantity_limit",    &battle_config.atc_spawn_quantity_limit	},
	{ "attribute_recover",                 &battle_config.attr_recover				},
	{ "backstab_bow_penalty",              &battle_config.backstab_bow_penalty	},
	{ "ban_spoof_namer",                   &battle_config.ban_spoof_namer	}, // added by [Yor]
	{ "base_exp_rate",						&battle_config.base_exp_rate			},
	{ "basic_skill_check",					&battle_config.basic_skill_check		},
	{ "battle_log",							&battle_config.battle_log				},
	{ "berserk_candels_buffs",				&battle_config.berserk_cancels_buffs}, // [Aru]
	{ "bone_drop",							&battle_config.bone_drop				},
	{ "boss_spawn_delay",					&battle_config.boss_spawn_delay			},
	{ "buyer_name",							&battle_config.buyer_name		},
	{ "cardillust_read_grffile",           &battle_config.cardillust_read_grffile},	// [Celest]
	{ "casting_rate",                      &battle_config.cast_rate				},
	{ "castle_defense_rate",               &battle_config.castle_defense_rate		},
	{ "castrate_dex_scale",                &battle_config.castrate_dex_scale	}, // added by [MouseJstr]
	{ "character_size",						&battle_config.character_size}, // [Lupus]
	{ "chat_warpportal",					&battle_config.chat_warpportal			},
	{ "combo_delay_rate",					&battle_config.combo_delay_rate			},
	{ "copyskill_restrict",					&battle_config.copyskill_restrict}, // [Aru]
	{ "day_duration",                      &battle_config.day_duration	}, // added by [Yor]
	{ "dead_branch_active",                &battle_config.dead_branch_active			},
	{ "death_penalty_base",                &battle_config.death_penalty_base		},
	{ "death_penalty_job",                 &battle_config.death_penalty_job		},
	{ "death_penalty_type",                &battle_config.death_penalty_type		},
	{ "defunit_not_enemy",                 &battle_config.defnotenemy				},
	{ "delay_battle_damage",				&battle_config.delay_battle_damage}, // [celest]
	{ "delay_dependon_dex",                &battle_config.delay_dependon_dex		},
	{ "delay_rate",                        &battle_config.delay_rate				},
	{ "devotion_level_difference",         &battle_config.devotion_level_difference	},
	{ "disp_experience",                   &battle_config.disp_experience			},
	{ "disp_hpmeter",                      &battle_config.disp_hpmeter				},
	{ "display_delay_skill_fail",          &battle_config.display_delay_skill_fail	},
	{ "display_hallucination",				&battle_config.display_hallucination	}, // [Skotlex]
	{ "display_snatcher_skill_fail",       &battle_config.display_snatcher_skill_fail	},
	{ "display_version",					&battle_config.display_version			}, // [Ancyker], for a feature by...?
	{ "drop_rate0item",                    &battle_config.drop_rate0item			},
	{ "drops_by_luk",                      &battle_config.drops_by_luk				},	// [Valaris]
	{ "dynamic_mobs",						&battle_config.dynamic_mobs				},
	{ "enemy_critical",                    &battle_config.enemy_critical			},
	{ "enemy_critical_rate",               &battle_config.enemy_critical_rate		},
	{ "enemy_perfect_flee",                &battle_config.enemy_perfect_flee		},
	{ "enemy_str",                         &battle_config.enemy_str					},
	{ "equip_natural_break_rate",          &battle_config.equip_natural_break_rate	},
	{ "equip_self_break_rate",             &battle_config.equip_self_break_rate		},
	{ "equip_skill_break_rate",            &battle_config.equip_skill_break_rate	},
	{ "error_log",                         &battle_config.error_log					},
	{ "etc_log",                           &battle_config.etc_log					},
	{ "exp_calc_type",						&battle_config.exp_calc_type			}, // [celest]
	{ "finding_ore_rate",					&battle_config.finding_ore_rate			}, // [celest]
	{ "finger_offensive_type",             &battle_config.finger_offensive_type		},
	{ "flooritem_lifetime",                &battle_config.flooritem_lifetime		},
	{ "gm_all_equipment",                  &battle_config.gm_allequip				},
	{ "gm_all_skill",                      &battle_config.gm_allskill				},
	{ "gm_all_skill_add_abra",	            &battle_config.gm_allskill_addabra		},
	{ "gm_can_drop_lv",                    &battle_config.gm_can_drop_lv			},
	{ "gm_join_chat",                      &battle_config.gm_join_chat				},
	{ "gm_kick_chat",                      &battle_config.gm_kick_chat				},
	{ "gm_skill_unconditional",            &battle_config.gm_skilluncond			},
	{ "gtb_pvp_only",                      &battle_config.gtb_pvp_only				},
	{ "guild_emperium_check",              &battle_config.guild_emperium_check		},
	{ "guild_exp_limit",                   &battle_config.guild_exp_limit			},
	{ "guild_max_castles",                 &battle_config.guild_max_castles			},
	{ "gvg_eliminate_time",                &battle_config.gvg_eliminate_time		},
	{ "gvg_long_attack_damage_rate",       &battle_config.gvg_long_damage_rate		},
	{ "gvg_magic_attack_damage_rate",      &battle_config.gvg_magic_damage_rate		},
	{ "gvg_misc_attack_damage_rate",       &battle_config.gvg_misc_damage_rate		},
	{ "gvg_short_attack_damage_rate",      &battle_config.gvg_short_damage_rate		},
	{ "gx_allhit",                         &battle_config.gx_allhit					},
	{ "gx_cardfix",                        &battle_config.gx_cardfix				},
	{ "gx_disptype",                       &battle_config.gx_disptype				},
	{ "gx_dupele",                         &battle_config.gx_dupele					},
	{ "hack_info_GM_level",                &battle_config.hack_info_GM_level		}, // added by [Yor]
	{ "headset_block_music",				&battle_config.headset_block_music		}, // [Lupus]
	{ "heal_exp",							&battle_config.heal_exp					},
	{ "hide_GM_session",					&battle_config.hide_GM_session			},
	{ "holywater_name_input",				&battle_config.holywater_name_input		},
	{ "hp_rate",							&battle_config.hp_rate					},
	{ "idle_no_share",						&battle_config.idle_no_share}, // [celest], for a feature by [MouseJstr]
	{ "ignore_items_gender",				&battle_config.ignore_items_gender}, // [Lupus]
	{ "indoors_override_grffile",          &battle_config.indoors_override_grffile},	// [Celest]
	{ "invite_request_check",              &battle_config.invite_request_check		},
	{ "item_auto_get",                     &battle_config.item_auto_get			},
	{ "item_check",                        &battle_config.item_check				},
	{ "item_drop_card_max",                &battle_config.item_drop_card_max	},
	{ "item_drop_card_min",                &battle_config.item_drop_card_min	},
	{ "item_drop_common_max",              &battle_config.item_drop_common_max	},
	{ "item_drop_common_min",              &battle_config.item_drop_common_min	},	// Added by TyrNemesis^
	{ "item_drop_equip_max",               &battle_config.item_drop_equip_max	},
	{ "item_drop_equip_min",               &battle_config.item_drop_equip_min	},
	{ "item_drop_heal_max",                 &battle_config.item_drop_heal_max	},
	{ "item_drop_heal_min",                 &battle_config.item_drop_heal_min	},
	{ "item_drop_mvp_max",                 &battle_config.item_drop_mvp_max	},	// End Addition
	{ "item_drop_mvp_min",                 &battle_config.item_drop_mvp_min	},
	{ "item_drop_use_max",                 &battle_config.item_drop_use_max	},
	{ "item_drop_use_min",                 &battle_config.item_drop_use_min	},
	{ "item_equip_override_grffile",       &battle_config.item_equip_override_grffile},	// [Celest]
	{ "item_first_get_time",				&battle_config.item_first_get_time		},
	{ "item_name_override_grffile",        &battle_config.item_name_override_grffile},
	{ "item_rate_card",						&battle_config.item_rate_card	},	// End Addition
	{ "item_rate_common",					&battle_config.item_rate_common	},	// Added by RoVeRT
	{ "item_rate_equip",					&battle_config.item_rate_equip	},
	{ "item_rate_heal",						&battle_config.item_rate_heal	},	// Added by Valaris
	{ "item_rate_use",						&battle_config.item_rate_use	},	// End
	{ "item_second_get_time",				&battle_config.item_second_get_time		},
	{ "item_slots_override_grffile",       &battle_config.item_slots_override_grffile},	// [Celest]
	{ "item_third_get_time",				&battle_config.item_third_get_time		},
	{ "item_use_interval",                 &battle_config.item_use_interval	},
	{ "job_exp_rate",						&battle_config.job_exp_rate				},
	{ "left_cardfix_to_right",             &battle_config.left_cardfix_to_right	},
	{ "magic_defense_type",                &battle_config.magic_defense_type		},
	{ "mail_system",						&battle_config.mail_system	}, // added by [Valaris]
	{ "making_arrow_name_input",           &battle_config.making_arrow_name_input	},
	{ "max_adv_level",						&battle_config.max_adv_level				},
	{ "max_aspd",                          &battle_config.max_aspd					},
	{ "max_base_level",						&battle_config.max_base_level				},
	{ "max_cart_weight",                   &battle_config.max_cart_weight			},
	{ "max_cloth_color",                   &battle_config.max_cloth_color	}, // added by [MouseJstr]
	{ "max_hair_color",                    &battle_config.max_hair_color	}, // added by [MouseJstr]
	{ "max_hair_style",                    &battle_config.max_hair_style	}, // added by [MouseJstr]
	{ "max_hitrate",                       &battle_config.max_hitrate	},
	{ "max_hp",                            &battle_config.max_hp					},
	{ "max_job_level",						&battle_config.max_job_level				},
	{ "max_parameter",                     &battle_config.max_parameter			},
	{ "max_sn_level",						&battle_config.max_sn_level				},
	{ "max_sp",                            &battle_config.max_sp					},
	{ "max_walk_speed",						&battle_config.max_walk_speed			},
	{ "maximum_level",                     &battle_config.maximum_level	},	// [Valaris]
	{ "min_cloth_color",                   &battle_config.min_cloth_color	}, // added by [MouseJstr]
	{ "min_hair_color",                    &battle_config.min_hair_color	}, // added by [MouseJstr]
	{ "min_hair_style",                    &battle_config.min_hair_style	}, // added by [MouseJstr]
	{ "min_hitrate",                       &battle_config.min_hitrate	},
	{ "min_skill_delay_limit",             &battle_config.min_skill_delay_limit}, // [celest]
	{ "mob_attack_attr_none",              &battle_config.mob_attack_attr_none		},
	{ "mob_changetarget_byskill",          &battle_config.mob_changetarget_byskill},
	{ "mob_clear_delay",					&battle_config.mob_clear_delay	},
	{ "mob_count_rate",                    &battle_config.mob_count_rate			},
	{ "mob_ghostring_fix",                 &battle_config.mob_ghostring_fix		},
	{ "mob_remove_damaged",                &battle_config.mob_remove_damaged},
	{ "mob_remove_delay",					&battle_config.mob_remove_delay	},
	{ "mob_skill_delay",                   &battle_config.mob_skill_delay			},
	{ "mob_skill_rate",                    &battle_config.mob_skill_rate			},
	{ "mob_spawn_delay",                   &battle_config.mob_spawn_delay			},
	{ "mob_warpportal",                    &battle_config.mob_warpportal			},
	{ "mobs_level_up",                     &battle_config.mobs_level_up}, // [Valaris]
	{ "monster_active_enable",             &battle_config.monster_active_enable	},
	{ "monster_attack_direction_change",   &battle_config.monster_attack_direction_change },
	{ "monster_auto_counter_type",         &battle_config.monster_auto_counter_type},
	{ "monster_class_change_full_recover", &battle_config.monster_class_change_full_recover },
	{ "monster_cloak_check_type",          &battle_config.monster_cloak_check_type	},
	{ "monster_damage_delay",              &battle_config.monster_damage_delay		},
	{ "monster_damage_delay_rate",         &battle_config.monster_damage_delay_rate},
	{ "monster_defense_type",              &battle_config.monster_defense_type		},
	{ "monster_hp_rate",                   &battle_config.monster_hp_rate			},
	{ "monster_land_skill_limit",          &battle_config.monster_land_skill_limit},
	{ "monster_loot_type",                 &battle_config.monster_loot_type		},
	{ "monster_max_aspd",                  &battle_config.monster_max_aspd			},
	{ "monster_skill_add_range",           &battle_config.mob_skill_add_range		},
	{ "monster_skill_log",                 &battle_config.mob_skill_log			},
	{ "monster_skill_nofootset",           &battle_config.monster_skill_nofootset	},
	{ "monster_skill_reiteration",         &battle_config.monster_skill_reiteration},
	{ "monsters_ignore_gm",                &battle_config.monsters_ignore_gm	},	// [Valaris]
	{ "motd_type",							&battle_config.motd_type}, // [celest]
	{ "multi_level_up",                    &battle_config.multi_level_up		}, // [Valaris]
	{ "muting_players",                    &battle_config.muting_players}, // added by [Apple]
	{ "mvp_exp_rate",						&battle_config.mvp_exp_rate				},
	{ "mvp_hp_rate",                       &battle_config.mvp_hp_rate				},
	{ "mvp_item_first_get_time",           &battle_config.mvp_item_first_get_time	},
	{ "mvp_item_rate",						&battle_config.mvp_item_rate			},
	{ "mvp_item_second_get_time",          &battle_config.mvp_item_second_get_time	},
	{ "mvp_item_third_get_time",           &battle_config.mvp_item_third_get_time	},
	{ "natural_heal_skill_interval",		&battle_config.natural_heal_skill_interval},
	{ "natural_heal_weight_rate",          &battle_config.natural_heal_weight_rate	},
	{ "natural_healhp_interval",           &battle_config.natural_healhp_interval	},
	{ "natural_healsp_interval",           &battle_config.natural_healsp_interval	},
	{ "night_at_start",                    &battle_config.night_at_start	}, // added by [Yor]
	{ "night_darkness_level",              &battle_config.night_darkness_level}, // [celest]
	{ "night_duration",                    &battle_config.night_duration	}, // added by [Yor]
	{ "packet_ver_flag",                   &battle_config.packet_ver_flag	}, // added by [Yor]
	{ "party_bonus",						&battle_config.party_bonus	}, // added by [Valaris]
	{ "party_share_mode",					&battle_config.party_share_mode			},
	{ "party_skill_penalty",               &battle_config.party_skill_penalty		},
	{ "pc_attack_attr_none",               &battle_config.pc_attack_attr_none		},
	{ "pet_attack_attr_none",              &battle_config.pet_attack_attr_none		},
	{ "pet_attack_exp_rate",               &battle_config.pet_attack_exp_rate	 },
	{ "pet_attack_exp_to_master",          &battle_config.pet_attack_exp_to_master	},
	{ "pet_attack_support",                &battle_config.pet_attack_support		},
	{ "pet_catch_rate",                    &battle_config.pet_catch_rate			},
	{ "pet_damage_support",                &battle_config.pet_damage_support		},
	{ "pet_defense_type",                  &battle_config.pet_defense_type			},
	{ "pet_equip_required",                &battle_config.pet_equip_required	},	// [Valaris]
	{ "pet_friendly_rate",                 &battle_config.pet_friendly_rate		},
	{ "pet_hungry_delay_rate",             &battle_config.pet_hungry_delay_rate	},
	{ "pet_hungry_friendly_decrease",      &battle_config.pet_hungry_friendly_decrease},
	{ "pet_lv_rate",                       &battle_config.pet_lv_rate				},	//Skotlex
	{ "pet_max_atk1",                      &battle_config.pet_max_atk1				},	//Skotlex
	{ "pet_max_atk2",                      &battle_config.pet_max_atk2				},	//Skotlex
	{ "pet_max_stats",                     &battle_config.pet_max_stats				},	//Skotlex
	{ "pet_random_move",					&battle_config.pet_random_move			},
	{ "pet_rename",                        &battle_config.pet_rename				},
	{ "pet_status_support",                &battle_config.pet_status_support		},
	{ "pet_str",                           &battle_config.pet_str					},
	{ "pet_support_min_friendly",          &battle_config.pet_support_min_friendly	},
	{ "pet_support_rate",                  &battle_config.pet_support_rate			},
	{ "pk_min_level",                      &battle_config.pk_min_level}, // [celest]
	{ "pk_mode",                           &battle_config.pk_mode			},  	// [Valaris]
	{ "plant_spawn_delay",                 &battle_config.plant_spawn_delay			},
	{ "player_attack_direction_change",    &battle_config.pc_attack_direction_change },
	{ "player_auto_counter_type",          &battle_config.pc_auto_counter_type		},
	{ "player_cloak_check_type",           &battle_config.pc_cloak_check_type		},
	{ "player_damage_delay",               &battle_config.pc_damage_delay			},
	{ "player_damage_delay_rate",          &battle_config.pc_damage_delay_rate		},
	{ "player_defense_type",               &battle_config.player_defense_type		},
	{ "player_invincible_time",            &battle_config.pc_invincible_time		},
	{ "player_land_skill_limit",           &battle_config.pc_land_skill_limit		},
	{ "player_skill_add_range",            &battle_config.pc_skill_add_range		},
	{ "player_skill_log",                  &battle_config.pc_skill_log				},
	{ "player_skill_nofootset",            &battle_config.pc_skill_nofootset		},
	{ "player_skill_partner_check",        &battle_config.player_skill_partner_check},
	{ "player_skill_reiteration",          &battle_config.pc_skill_reiteration		},
	{ "player_skillfree",                  &battle_config.skillfree				},
	{ "player_skillup_limit",              &battle_config.skillup_limit			},
	{ "potion_produce_rate",               &battle_config.pp_rate					},
	{ "prevent_logout",                    &battle_config.prevent_logout		},	// Added by RoVeRT
	{ "produce_item_name_input",           &battle_config.produce_item_name_input	},
	{ "produce_potion_name_input",         &battle_config.produce_potion_name_input},
	{ "pvp_exp",                           &battle_config.pvp_exp		},
	{ "quest_skill_learn",                 &battle_config.quest_skill_learn		},
	{ "quest_skill_reset",                 &battle_config.quest_skill_reset		},
	{ "rainy_waterball",					&battle_config.rainy_waterball}, // [Shinomori]
	{ "random_monster_checklv",            &battle_config.random_monster_checklv	},
	{ "require_glory_guild",				&battle_config.require_glory_guild}, // [celest]
	{ "restart_hp_rate",                   &battle_config.restart_hp_rate			},
	{ "restart_sp_rate",                   &battle_config.restart_sp_rate			},
	{ "resurrection_exp",                  &battle_config.resurrection_exp			},
	{ "save_clothcolor",                   &battle_config.save_clothcolor			},
	{ "save_log",                          &battle_config.save_log					},
	{ "shop_exp",                          &battle_config.shop_exp					},
	{ "show_hp_sp_drain",					&battle_config.show_hp_sp_drain}, // [Skotlex]
	{ "show_hp_sp_gain",					&battle_config.show_hp_sp_gain}, // [Skotlex]
	{ "show_mob_hp",                       &battle_config.show_mob_hp	}, // [Valaris]
	{ "show_steal_in_same_party",          &battle_config.show_steal_in_same_party		},
	{ "skill_delay_attack_enable",         &battle_config.sdelay_attack_enable		},
	{ "skill_min_damage",                  &battle_config.skill_min_damage			},
	{ "skill_out_range_consume",           &battle_config.skill_out_range_consume	},
	{ "skill_removetrap_type",             &battle_config.skill_removetrap_type	},
	{ "skill_sp_override_grffile",         &battle_config.skill_sp_override_grffile},	// [Celest]
	{ "skill_steal_rate",                  &battle_config.skill_steal_rate}, // [celest]
	{ "skill_steal_type",                  &battle_config.skill_steal_type}, // [celest]
	{ "sp_rate",                           &battle_config.sp_rate					},
	{ "undead_detect_type",                &battle_config.undead_detect_type		},
	{ "unit_movement_type",                &battle_config.unit_movement_type		},
	{ "use_statpoint_table",				&battle_config.use_statpoint_table}, // [Skotlex]
	{ "vending_max_value",                 &battle_config.vending_max_value		},
	{ "vit_penalty_count",                 &battle_config.vit_penalty_count			},
	{ "vit_penalty_count_lv",              &battle_config.vit_penalty_count_lv		},
	{ "vit_penalty_num",                   &battle_config.vit_penalty_num			},
	{ "vit_penalty_type",                  &battle_config.vit_penalty_type			},
	{ "warp_point_debug",                  &battle_config.warp_point_debug			},
	{ "weapon_produce_rate",				&battle_config.wp_rate					},
	{ "wedding_ignorepalette",				&battle_config.wedding_ignorepalette	},
	{ "wedding_modifydisplay",				&battle_config.wedding_modifydisplay	},
	{ "who_display_aid",					&battle_config.who_display_aid			}, // [Ancyker], for a feature by...?
	{ "zeny_from_mobs",						&battle_config.zeny_from_mobs			}, // [Valaris]
	{ "zeny_penalty",						&battle_config.zeny_penalty				},
};

int battle_set_value(const char *w1, const char *w2)
{
	size_t i;
	for(i = 0; i < sizeof(battle_data) / (sizeof(battle_data[0])); i++)
	{
		if(battle_data[i].val && battle_data[i].str && strcasecmp(w1, battle_data[i].str) == 0)
		{
			*(battle_data[i].val) = config_switch(w2);
			return 1;
		}
	}
	return 0;
}

void battle_set_defaults()
{
	battle_config.agi_penalty_count = 3;
	battle_config.agi_penalty_count_lv = ATK_FLEE;
	battle_config.agi_penalty_num = 10;
	battle_config.agi_penalty_type = 1;
	battle_config.alchemist_summon_reward = 0;
	battle_config.allow_atcommand_when_mute = 0;
	battle_config.any_warp_GM_min_level = 60; // added by [Yor]
	battle_config.area_size = 14;
	battle_config.arrow_decrement=1;
	battle_config.atc_gmonly=0;
	battle_config.atc_spawn_quantity_limit=0;
	battle_config.attr_recover=1;
	battle_config.backstab_bow_penalty = 0; // Akaru
	battle_config.ban_hack_trade=1;
	battle_config.ban_spoof_namer = 5; // added by [Yor] (default: 5 minutes)
	battle_config.base_exp_rate=100;
	battle_config.basic_skill_check=1;
	battle_config.battle_log = 0;
	battle_config.berserk_cancels_buffs = 0;
	battle_config.bone_drop = 0;
	battle_config.boss_spawn_delay=100;	
	battle_config.buyer_name = 1;
	battle_config.character_size = 3; //3: Peco riders Size=2, Baby Class Riders Size=1
	battle_config.cardillust_read_grffile=0;
	battle_config.cast_rate=100;
	battle_config.castle_defense_rate = 100;
	battle_config.castrate_dex_scale = 150;
	battle_config.chat_warpportal = 0;
	battle_config.combo_delay_rate=100;
	battle_config.copyskill_restrict=0;
	battle_config.day_duration = 2*60*60*1000; // added by [Yor] (2 hours)
	battle_config.dead_branch_active = 0;
	battle_config.death_penalty_base=0;
	battle_config.death_penalty_job=0;
	battle_config.death_penalty_type=0;
	battle_config.defnotenemy=1;
	battle_config.delay_battle_damage = 1;
	battle_config.delay_dependon_dex=0;
	battle_config.delay_rate=100;
	battle_config.devotion_level_difference = 10;
	battle_config.disp_experience = 0;
	battle_config.disp_hpmeter = 60;
	battle_config.display_delay_skill_fail = 1;
	battle_config.display_hallucination = 1;
	battle_config.display_snatcher_skill_fail = 1;
	battle_config.display_version = 1;
	battle_config.drop_rate0item=0;
	battle_config.drops_by_luk = 0;
	battle_config.dynamic_mobs = 1;
	battle_config.enemy_critical_rate=100;
	battle_config.enemy_critical=0;
	battle_config.enemy_perfect_flee=0;
	battle_config.enemy_str=1;
	battle_config.equip_natural_break_rate = 1;
	battle_config.equip_self_break_rate = 100; // [Valaris], adapted by [Skotlex]
	battle_config.equip_skill_break_rate = 100; // [Valaris], adapted by [Skotlex]
	battle_config.error_log = 1;
	battle_config.etc_log = 1;
	battle_config.exp_calc_type = 1;
	battle_config.finding_ore_rate = 100;
	battle_config.finger_offensive_type=0;
	battle_config.flooritem_lifetime=LIFETIME_FLOORITEM*1000;
	battle_config.gm_allequip=0;
	battle_config.gm_allskill=0;
	battle_config.gm_allskill_addabra=0;
	battle_config.gm_can_drop_lv = 0;
	battle_config.gm_join_chat=0;
	battle_config.gm_kick_chat=0;
	battle_config.gm_skilluncond=0;
	battle_config.gtb_pvp_only=0;
	battle_config.guild_emperium_check=1;
	battle_config.guild_exp_limit=50;
	battle_config.guild_max_castles=0;
	battle_config.gvg_eliminate_time = 7000;
	battle_config.gvg_long_damage_rate = 60;
	battle_config.gvg_magic_damage_rate = 50;
	battle_config.gvg_misc_damage_rate = 60;
	battle_config.gvg_short_damage_rate = 100;
	battle_config.gx_allhit = 0;
	battle_config.gx_cardfix = 0;
	battle_config.gx_disptype = 1;
	battle_config.gx_dupele = 1;
	battle_config.hack_info_GM_level = 60; // added by [Yor] (default: 60, GM level)
	battle_config.headset_block_music = 0; //Do headsets block some sound skills like Frost Joke
	battle_config.heal_exp=0;
	battle_config.hide_GM_session = 0;
	battle_config.holywater_name_input = 1;
	battle_config.hp_rate = 100;
	battle_config.idle_no_share = 0;
	battle_config.ignore_items_gender = 1;
	battle_config.indoors_override_grffile=0;
	battle_config.invite_request_check = 1;
	battle_config.item_auto_get=0;
	battle_config.item_check=1;
	battle_config.item_drop_card_max=10000;
	battle_config.item_drop_card_min=1;
	battle_config.item_drop_common_max=10000;
	battle_config.item_drop_common_min=1;
	battle_config.item_drop_equip_max=10000;
	battle_config.item_drop_equip_min=1;
	battle_config.item_drop_heal_max=10000;
	battle_config.item_drop_heal_min=1;
	battle_config.item_drop_mvp_max=10000;
	battle_config.item_drop_mvp_min=1;
	battle_config.item_drop_use_max=10000;
	battle_config.item_drop_use_min=1;
	battle_config.item_equip_override_grffile=0;
	battle_config.item_first_get_time=3000;
	battle_config.item_name_override_grffile=1;
	battle_config.item_rate_card = 100;
	battle_config.item_rate_common = 100;
	battle_config.item_rate_equip = 100;
	battle_config.item_rate_heal = 100;
	battle_config.item_rate_use = 100;
	battle_config.item_second_get_time=1000;
	battle_config.item_slots_override_grffile=0;
	battle_config.item_third_get_time=1000;
	battle_config.item_use_interval=500;
	battle_config.job_exp_rate=100;
	battle_config.left_cardfix_to_right=0;
	battle_config.magic_defense_type = 0;
	battle_config.mail_system = 0;
	battle_config.making_arrow_name_input = 1;
	battle_config.max_adv_level=70;
	battle_config.max_aspd = 199;
	battle_config.max_aspd_interval=10;
	battle_config.max_base_level = 99; // [MouseJstr]
	battle_config.max_cart_weight = 8000;
	battle_config.max_cloth_color = 4;
	battle_config.max_hair_color = 9;
	battle_config.max_hair_style = 20;
	battle_config.max_hitrate = 95;
	battle_config.max_hp = 32500;
	battle_config.max_job_level = 50; // [MouseJstr]
	battle_config.max_parameter = 99;
	battle_config.max_sn_level = 70;
	battle_config.max_sp = 32500;
	battle_config.max_walk_speed = 300;
	battle_config.maximum_level = 255;
	battle_config.min_cloth_color = 0;
	battle_config.min_hair_color = 0;
	battle_config.min_hair_style = 0;
	battle_config.min_hitrate = 5;
	battle_config.min_skill_delay_limit = 100;
	battle_config.mob_attack_attr_none = 1;
	battle_config.mob_changetarget_byskill = 0;
	battle_config.mob_clear_delay=0;
	battle_config.mob_count_rate=100;
	battle_config.mob_ghostring_fix = 0;
	battle_config.mob_remove_damaged = 0;
	battle_config.mob_remove_delay = 60000;
	battle_config.mob_skill_add_range=0;
	battle_config.mob_skill_delay=100;
	battle_config.mob_skill_log = 0;
	battle_config.mob_skill_rate=100;
	battle_config.mob_spawn_delay=100;
	battle_config.mob_warpportal = 0;
	battle_config.mobs_level_up = 0;
	battle_config.monster_active_enable=1;
	battle_config.monster_attack_direction_change = 1;
	battle_config.monster_auto_counter_type = 1;
	battle_config.monster_class_change_full_recover = 0;
	battle_config.monster_cloak_check_type = 0;
	battle_config.monster_damage_delay = 1;
	battle_config.monster_damage_delay_rate=100;
	battle_config.monster_defense_type = 0;
	battle_config.monster_hp_rate=100;
	battle_config.monster_land_skill_limit = 1;
	battle_config.monster_loot_type=0;
	battle_config.monster_max_aspd=199;
	battle_config.monster_skill_nofootset = 0;
	battle_config.monster_skill_reiteration = 0;
	battle_config.monsters_ignore_gm=0;
	battle_config.motd_type = 0;
	battle_config.multi_level_up = 0; // [Valaris]
	battle_config.muting_players=0;
	battle_config.mvp_exp_rate=100;
	battle_config.mvp_hp_rate=100;
	battle_config.mvp_item_first_get_time=10000;
	battle_config.mvp_item_rate=100;
	battle_config.mvp_item_second_get_time=10000;
	battle_config.mvp_item_third_get_time=2000;
	battle_config.natural_heal_skill_interval=10000;
	battle_config.natural_heal_weight_rate=50;
	battle_config.natural_healhp_interval=6000;
	battle_config.natural_healsp_interval=8000;
	battle_config.new_attack_function = 0; //This is for test/debug purposes [Skotlex]
	battle_config.night_at_start = 0; // added by [Yor]
	battle_config.night_darkness_level = 9;
	battle_config.night_duration = 30*60*1000; // added by [Yor] (30 minutes)
	battle_config.packet_ver_flag = 255; // added by [Yor]
	battle_config.party_bonus = 0;
	battle_config.party_share_mode = 2; // 0 exclude none, 1 exclude idle, 2 exclude idle+chatting
	battle_config.party_skill_penalty = 1;
	battle_config.pc_attack_attr_none = 0;
	battle_config.pc_attack_direction_change = 1;
	battle_config.pc_auto_counter_type = 1;
	battle_config.pc_cloak_check_type = 0;
	battle_config.pc_damage_delay_rate=100;
	battle_config.pc_damage_delay=1;
	battle_config.pc_invincible_time = 5000;
	battle_config.pc_land_skill_limit = 1;
	battle_config.pc_skill_add_range=0;
	battle_config.pc_skill_log = 1;
	battle_config.pc_skill_nofootset = 0;
	battle_config.pc_skill_reiteration = 0;
	battle_config.pet_attack_attr_none = 0;
	battle_config.pet_attack_exp_rate=100;
	battle_config.pet_attack_exp_to_master=0;
	battle_config.pet_attack_support=0;
	battle_config.pet_catch_rate=100;
	battle_config.pet_damage_support=0;
	battle_config.pet_defense_type = 0;
	battle_config.pet_equip_required = 0; // [Valaris]
	battle_config.pet_friendly_rate=100;
	battle_config.pet_hungry_delay_rate=100;
	battle_config.pet_hungry_friendly_decrease=5;
	battle_config.pet_lv_rate=0;
	battle_config.pet_max_atk1=750;
	battle_config.pet_max_atk2=1000;
	battle_config.pet_max_stats=99;
	battle_config.pet_random_move=1;
	battle_config.pet_rename=0;
	battle_config.pet_status_support=0;
	battle_config.pet_str=1;
	battle_config.pet_support_min_friendly=900;
	battle_config.pet_support_rate=100;
	battle_config.pk_min_level = 55;
	battle_config.pk_mode = 0; // [Valaris]
	battle_config.plant_spawn_delay=100;
	battle_config.player_defense_type = 0;
	battle_config.player_skill_partner_check = 1;
	battle_config.pp_rate=100;
	battle_config.prevent_logout = 1;
	battle_config.produce_item_name_input = 1;
	battle_config.produce_potion_name_input = 1;
	battle_config.pvp_exp=1;
	battle_config.quest_skill_learn=0;
	battle_config.quest_skill_reset=1;
	battle_config.rainy_waterball = 1;
	battle_config.random_monster_checklv=1;
	battle_config.require_glory_guild = 0;
	battle_config.restart_hp_rate=0;
	battle_config.restart_sp_rate=0;
	battle_config.resurrection_exp=0;
	battle_config.save_clothcolor = 0;
	battle_config.save_log = 0;
	battle_config.sdelay_attack_enable=0;
	battle_config.shop_exp=100;
	battle_config.show_hp_sp_drain = 0; //Display drained hp/sp from attacks
	battle_config.show_hp_sp_gain = 1;
	battle_config.show_mob_hp = 0; // [Valaris]
	battle_config.show_steal_in_same_party = 0;
	battle_config.skill_min_damage=0;
	battle_config.skill_out_range_consume=1;
	battle_config.skill_removetrap_type = 0;
	battle_config.skill_sp_override_grffile=0;
	battle_config.skill_steal_rate = 100;
	battle_config.skill_steal_type = 1;
	battle_config.skillfree = 0;
	battle_config.skillup_limit = 0;
	battle_config.sp_rate = 100;
	battle_config.undead_detect_type = 0;
	battle_config.unit_movement_type = 0;
	battle_config.use_statpoint_table = 1;
	battle_config.vending_max_value = 10000000;
	battle_config.vit_penalty_count = 3;
	battle_config.vit_penalty_count_lv = ATK_DEF;
	battle_config.vit_penalty_num = 5;
	battle_config.vit_penalty_type = 1;
	battle_config.warp_point_debug=0;
	battle_config.wedding_ignorepalette=0;
	battle_config.wedding_modifydisplay=0;
	battle_config.who_display_aid = 0;
	battle_config.wp_rate=100;
	battle_config.zeny_from_mobs = 0;
	battle_config.zeny_penalty=0;
}

void battle_validate_conf()
{
	if(battle_config.flooritem_lifetime < 1000)
		battle_config.flooritem_lifetime = LIFETIME_FLOORITEM*1000;
	if(battle_config.restart_hp_rate > 100)
		battle_config.restart_hp_rate = 100;
	if(battle_config.restart_sp_rate > 100)
		battle_config.restart_sp_rate = 100;
	if(battle_config.natural_healhp_interval < NATURAL_HEAL_INTERVAL)
		battle_config.natural_healhp_interval=NATURAL_HEAL_INTERVAL;
	if(battle_config.natural_healsp_interval < NATURAL_HEAL_INTERVAL)
		battle_config.natural_healsp_interval=NATURAL_HEAL_INTERVAL;
	if(battle_config.natural_heal_skill_interval < NATURAL_HEAL_INTERVAL)
		battle_config.natural_heal_skill_interval=NATURAL_HEAL_INTERVAL;
	if(battle_config.natural_heal_weight_rate < 50)
		battle_config.natural_heal_weight_rate = 50;
	if(battle_config.natural_heal_weight_rate > 101)
		battle_config.natural_heal_weight_rate = 101;
	
	////////////////////////////////////////////////
	if( battle_config.monster_max_aspd< 200 )
		battle_config.monster_max_aspd_interval = 2000 - battle_config.monster_max_aspd*10;
	else
		battle_config.monster_max_aspd_interval= 10;
	if(battle_config.monster_max_aspd_interval > 1000)
		battle_config.monster_max_aspd_interval = 1000;
	////////////////////////////////////////////////
	if(battle_config.max_aspd>199)
		battle_config.max_aspd_interval = 10;
	else if(battle_config.max_aspd<100)
		battle_config.max_aspd_interval = 1000;
	else
		battle_config.max_aspd_interval = 2000 - battle_config.max_aspd*10;
	////////////////////////////////////////////////
	if(battle_config.max_walk_speed > MAX_WALK_SPEED)
		battle_config.max_walk_speed = MAX_WALK_SPEED;


	if(battle_config.hp_rate < 1)
		battle_config.hp_rate = 1;
	if(battle_config.sp_rate < 1)
		battle_config.sp_rate = 1;
	if(battle_config.max_hp > 1000000)
		battle_config.max_hp = 1000000;
	if(battle_config.max_hp < 100)
		battle_config.max_hp = 100;
	if(battle_config.max_sp > 1000000)
		battle_config.max_sp = 1000000;
	if(battle_config.max_sp < 100)
		battle_config.max_sp = 100;
	if(battle_config.max_parameter < 10)
		battle_config.max_parameter = 10;
	if(battle_config.max_parameter > 10000)
		battle_config.max_parameter = 10000;
	if(battle_config.max_cart_weight > 1000000)
		battle_config.max_cart_weight = 1000000;
	if(battle_config.max_cart_weight < 100)
		battle_config.max_cart_weight = 100;
	battle_config.max_cart_weight *= 10;

	if(battle_config.min_hitrate > battle_config.max_hitrate)
		battle_config.min_hitrate = battle_config.max_hitrate;
		
	if(battle_config.agi_penalty_count < 2)
		battle_config.agi_penalty_count = 2;
	if(battle_config.vit_penalty_count < 2)
		battle_config.vit_penalty_count = 2;

	if(battle_config.guild_exp_limit > 99)
		battle_config.guild_exp_limit = 99;

	if(battle_config.pet_support_min_friendly > 950) //Capped to 950/1000 [Skotlex]
		battle_config.pet_support_min_friendly = 950;
	
	if(battle_config.pet_max_atk1 > battle_config.pet_max_atk2)	//Skotlex
		battle_config.pet_max_atk1 = battle_config.pet_max_atk2;
	
	if(battle_config.castle_defense_rate > 100)
		battle_config.castle_defense_rate = 100;
	if(battle_config.item_drop_common_min < 1)		// Added by TyrNemesis^
		battle_config.item_drop_common_min = 1;
	if(battle_config.item_drop_common_max > 10000)
		battle_config.item_drop_common_max = 10000;
	if(battle_config.item_drop_equip_min < 1)
		battle_config.item_drop_equip_min = 1;
	if(battle_config.item_drop_equip_max > 10000)
		battle_config.item_drop_equip_max = 10000;
	if(battle_config.item_drop_card_min < 1)
		battle_config.item_drop_card_min = 1;
	if(battle_config.item_drop_card_max > 10000)
		battle_config.item_drop_card_max = 10000;
	if(battle_config.item_drop_mvp_min < 1)
		battle_config.item_drop_mvp_min = 1;
	if(battle_config.item_drop_mvp_max > 10000)
		battle_config.item_drop_mvp_max = 10000;	// End Addition


	if (battle_config.night_at_start > 1) // added by [Yor]
		battle_config.night_at_start = 1;
	if (battle_config.day_duration != 0 && battle_config.day_duration < 60000) // added by [Yor]
		battle_config.day_duration = 60000;
	if (battle_config.night_duration != 0 && battle_config.night_duration < 60000) // added by [Yor]
		battle_config.night_duration = 60000;
	

	if (battle_config.ban_spoof_namer > 32767)
		battle_config.ban_spoof_namer = 32767;


	if (battle_config.hack_info_GM_level > 100)
		battle_config.hack_info_GM_level = 100;


	if (battle_config.any_warp_GM_min_level > 100)
		battle_config.any_warp_GM_min_level = 100;

	// at least 1 client must be accepted
	if ((battle_config.packet_ver_flag & 255) == 0) // added by [Yor]
		battle_config.packet_ver_flag = 255; // accept all clients

	if (battle_config.night_darkness_level > 10) // Celest
		battle_config.night_darkness_level = 10;


	if (battle_config.motd_type > 1)
		battle_config.motd_type = 1;

	if (battle_config.vending_max_value > 10000000 || battle_config.vending_max_value<=0) // Lupus & Kobra_k88
		battle_config.vending_max_value = 10000000;

	if (battle_config.min_skill_delay_limit < 10)
		battle_config.min_skill_delay_limit = 10;	// minimum delay of 10ms

	if (battle_config.mob_remove_delay < 15000)	//Min 15 sec
		battle_config.mob_remove_delay = 15000;
	if (battle_config.dynamic_mobs > 1)
		battle_config.dynamic_mobs = 1;	//The flag will be used in assignations	
}

/*==========================================
 * �ݒ�t�@�C����ǂݍ���
 *------------------------------------------
 */
int battle_config_read(const char *cfgName)
{
	char line[1024], w1[1024], w2[1024];
	FILE *fp;
	static int count = 0;

	if ((count++) == 0)
		battle_set_defaults();

	fp = safefopen(cfgName,"r");
	if (fp == NULL) {
		ShowError("file not found: %s\n", cfgName);
		return 1;
	}
	while(fgets(line,1020,fp)){
		if( !skip_empty_line(line) )
			continue;
		if (sscanf(line, "%[^:]:%s", w1, w2) != 2)
			continue;
		if(strcasecmp(w1, "import") == 0)
			battle_config_read(w2);
		else
		{
			if( !battle_set_value(w1, w2) )
				ShowWarning("(Battle Config) %s: no such option.\n", w1);
		}
	}
	fclose(fp);

	if (--count == 0) {
		battle_validate_conf();
		add_timer_func_list(battle_delay_damage_sub, "battle_delay_damage_sub");
	}

	return 0;
}