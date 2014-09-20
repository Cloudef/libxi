#ifndef __LIBXI_H__
#define __LIBXI_H__

#include <stdint.h>

/**
 * Data type constants.
 */
enum xi_data_type {
   XI_TYPE_NAME_ID,
   XI_TYPE_ABILITY,
   XI_TYPE_SPELL,
   XI_TYPE_ITEM,
   XI_TYPE_UNKNOWN,
};

enum xi_item_type {
   XI_ITEM_TYPE_NONE,
   XI_ITEM_TYPE_ITEM,
   XI_ITEM_TYPE_QUEST,
   XI_ITEM_TYPE_FISH,
   XI_ITEM_TYPE_WEAPON,
   XI_ITEM_TYPE_ARMOR,
   XI_ITEM_TYPE_LINKSHELL,
   XI_ITEM_TYPE_USABLE,
   XI_ITEM_TYPE_CRYSTAL,
   XI_ITEM_TYPE_FURNISHING,
   XI_ITEM_TYPE_PLANT,
   XI_ITEM_TYPE_FLOWERPOT,
   XI_ITEM_TYPE_PUPPET,
   XI_ITEM_TYPE_MANNEQUIN,
   XI_ITEM_TYPE_BOOK,
};

enum xi_item_flags {
   XI_ITEM_UNKNOWN0 = 1<<0,
   XI_ITEM_UNKNOWN1 = 1<<1,
   XI_ITEM_UNKNOWN2 = 1<<2,
   XI_ITEM_UNKNOWN3 = 1<<3,
   XI_ITEM_UNKNOWN4 = 1<<4,
   XI_ITEM_INSCRIBABLE = 1<<5,
   XI_ITEM_UNSELLABLE_TO_AH = 1<<6,
   XI_ITEM_SCROLL = 1<<7,
   XI_ITEM_LINKSHELL = 1<<8,
   XI_ITEM_USABLE = 1<<9,
   XI_ITEM_TRADEABLE_TO_NPC = 1<<10,
   XI_ITEM_EQUIPABLE = 1<<11,
   XI_ITEM_UNSELLABLE_TO_NPC = 1<<12,
   XI_ITEM_MOGHOUSE_DENIED = 1<<13,
   XI_ITEM_UNTRADEABLE = 1<<14,
   XI_ITEM_RARE = 1<<15,
   XI_ITEM_EX = 0x6040,
};

enum xi_target_flags {
   XI_TARGET_SELF = 1<<0,
   XI_TARGET_PLAYER = 1<<1,
   XI_TARGET_PARTY = 1<<2,
   XI_TARGET_ALLIANCE = 1<<3,
   XI_TARGET_NPC = 1<<4,
   XI_TARGET_ENEMY = 1<<5,
   XI_TARGET_UNKNOWN = 1<<6,
   XI_TARGET_CORPSE = 1<<7,
};

struct xi_string {
   char *data;
   size_t length;
   uint32_t flags;
};

/**
 * Name + ID pair.
 */
struct xi_name_id {
   char name[28]; // fixed width string, so not using xi_string
   // 0x010nnmmm
   // nn == zone mmm == monster/npc id
   uint32_t id;
};

/**
 * Ability.
 */
struct xi_ability {
   uint16_t index;
   uint16_t icon_id;
   uint16_t mp_cost;
   uint16_t unknown;
   uint16_t targets;
   char name[32];
   char description[256];
};

/**
 * Spell.
 */
struct xi_spell {
   uint16_t index;
   uint16_t type; // (1-6 for White/Black/Summon/Ninja/Bard/Blue)
   uint16_t element;
   uint16_t targets;
   uint16_t skill;
   uint16_t mp_cost;
   uint8_t casting_time; // (in quarter of seconds)
   uint8_t recast_delay; // (in quarter of seconds)
   uint8_t level[24]; // (1 byte per job, 0xFF if not learnable, first slot is NONE job so always 0xFF)
   uint16_t id; // (0 for "unused" spells; often, but not always, equal to Index)
   uint8_t unknown;
   char jp_name[20];
   char en_name[20];
   char jp_description[128];
   char en_description[128];
};

struct xi_item_weapon {
   uint16_t level;
   uint16_t slots;
   uint16_t races;
   uint32_t jobs;
   uint16_t damage;
   uint16_t delay;
   uint16_t dps;
   uint8_t skill;
   uint8_t jug_size;
   uint32_t unknown;
   uint8_t max_charges;
   uint8_t casting_time;
   uint16_t use_delay;
   uint32_t reuse_delay;
   uint32_t unknown2;
};

struct xi_item_armor {
   uint16_t level;
   uint16_t slots;
   uint16_t races;
   uint32_t jobs;
   uint16_t shield_size;
   uint8_t max_charges;
   uint8_t casting_time;
   uint16_t use_delay;
   uint16_t unknown;
   uint32_t reuse_delay;
   uint32_t unknown2;
};

struct xi_item_puppet {
   uint16_t slot;
   uint32_t element_charge;
   uint32_t unknown;
};

struct xi_item_general {
   uint16_t element;
   uint32_t storage_slots;
};

struct xi_item_usable {
   uint16_t activation_time;
   uint32_t unknown;
   uint32_t unknown2;
};

/**
 * Item.
 */
struct xi_item {
   uint32_t id;
   uint16_t flags;
   uint16_t stack;
   uint16_t type;
   uint16_t resource;
   uint16_t targets;

   union {
      struct xi_item_weapon *weapon;
      struct xi_item_armor *armor;
      struct xi_item_puppet *puppet;
      struct xi_item_general *general;
      struct xi_item_usable *usable;
      void *any;
   };

   struct xi_string *strings;
   uint32_t num_strings;
};

/**
 * Represents a type of data inside .dat archive.
 */
struct xi_data {
   enum xi_data_type type;

   union {
      struct xi_name_id *name_id;
      struct xi_ability *ability;
      struct xi_spell *spell;
      struct xi_item *item;
      void *any;
   };
};

/**
 * Represents a .dat archive.
 */
struct xi_archive;

/**
 * Represents a file table.
 */
struct xi_ftable;

struct xi_archive*
xi_archive_new(void);

void
xi_archive_free(struct xi_archive *archive);

struct xi_archive*
xi_archive_load_from_memory(const void *data, const size_t size);

struct xi_archive*
xi_archive_load_from_file(const char *file);

const struct xi_data*
xi_archive_get_data_list(struct xi_archive *archive, size_t *out_count);

void
xi_ftable_free(struct xi_ftable *ftable);

struct xi_ftable*
xi_ftable_load_from_memory(const void *f_data, const size_t f_size, const void *v_data, const size_t v_size);

const struct xi_ftable*
xi_ftable_load_from_file(const char *f_ftable, const char *f_vtable);

#endif /* __LIBXI_H__ */
