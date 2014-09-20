#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

#include "xi.h"
#include "buffer/buffer.h"
#include "pool/pool.h"

#ifndef MIN
#  define MIN(a,b) (((a)<(b))?(a):(b))
#endif

struct xi_archive {
   chckIterPool *data;
};

struct xi_file_entry {
   uint16_t id;
   uint8_t exist;
};

struct xi_ftable {
   chckIterPool *data;
};

static const size_t xi_data_sizes[] = {
   sizeof(struct xi_name_id), // XI_TYPE_NAME_ID,
   sizeof(struct xi_ability), // XI_TYPE_ABILITY,
   sizeof(struct xi_ability), // XI_TYPE_SPELL,
   sizeof(struct xi_item),    // XI_TYPE_ITEM,
   0,                         // XI_TYPE_UNKNOWN,
};

static void
id_to_path(const uint16_t id, char path[20])
{
   memset(path, 0, 20);
   snprintf(path, 20, "ROM\\%u\\%u.DAT", id >> 7, id & 0x7F);
}

static uint8_t
rotate_right(uint8_t b, int count)
{
   for (; count > 0; --count) {
      if ((b & 0x01) == 0x01) {
         b >>= 1; // if the last bit is 1 (ex. 00000001, it needs to be dropped
         b |= 0x80; // and then set as the first bit (ex. 10000000)
      } else b >>= 1; // if the last bit is not 1 (set), just rotate as normal.
   }
   return b;
}

static void
decode(uint8_t *data, const size_t size, const int count)
{
   for (size_t i = 0; i < size; ++i)
      data[i] = rotate_right(data[i], count);
}

static int
countbits(const uint8_t byte)
{
   static const uint8_t NIBBLE_LOOKUP [16] = {
      0, 1, 1, 2, 1, 2, 2, 3,
      1, 2, 2, 3, 2, 3, 3, 4
   };

   return NIBBLE_LOOKUP[byte & 0x0F] + NIBBLE_LOOKUP[byte >> 4];
}

static int
rotation_for_text_encryption(const uint8_t *data, const size_t size)
{
   if (size < 2 || (data[0] == 0 && data[1] == 0))
      return 0;

   int seed = countbits(data[1]) - countbits(data[0]);
   switch (abs(seed) % 5) {
      case 0: return 1;
      case 1: return 7;
      case 2: return 2;
      case 3: return 6;
      case 4: return 3;
      default:break;
   }

   assert(0 && "failed to detect rotation");
   return 0;
}

static int
rotation_for_variable_encryption(const uint8_t *data, const size_t size)
{
   if (size < 13)
      return 0;

   int seed = countbits(data[2]) - countbits(data[11]) + countbits(data[12]);
   switch (abs(seed) % 5) {
      case 0: return 7;
      case 1: return 1;
      case 2: return 6;
      case 3: return 2;
      case 4: return 5;
      default:break;
   }

   assert(0 && "failed to detect rotation");
   return 0;
}

static void*
data_from_file(const char *file, size_t *out_size)
{
   assert(file && out_size);

   FILE *f;
   void *data = NULL;
   *out_size = 0;

   if (!(f = fopen(file, "rb")))
      goto fail;

   fseek(f, 0L, SEEK_END);
   *out_size = ftell(f);
   fseek(f, 0L, SEEK_SET);

   if (!(data = malloc(*out_size)))
      goto fail;

   fread(data, 1, *out_size, f);
   fclose(f);
   return data;

fail:
   if (f)
      fclose(f);
   free(data);
   return NULL;
}

static void
item_free(struct xi_item *item)
{
   assert(item);

   for (uint32_t i = 0; i < item->num_strings; ++i)
      free(item->strings[i].data);

   free(item->strings);
   free(item->any);
   free(item);
}

static void
data_free(struct xi_data *data)
{
   assert(data);

   switch (data->type) {
      case XI_TYPE_ITEM:
         item_free(data->item);
         break;
      default:
         free(data->any);
         break;
   }
}

static int
item_set_data(struct xi_item *item, const size_t size, const void *data)
{
   assert(item && size && data);

   if (!(item->any = malloc(size)))
      return 0;

   memcpy(item->any, data, size);
   return 1;
}

static int
archive_add_data(struct xi_archive *archive, const enum xi_data_type type, const void *data)
{
   assert(archive);

   void *copy = NULL;
   if (type != XI_TYPE_UNKNOWN) {
      if (!(copy = malloc(xi_data_sizes[type])))
         return 0;

      memcpy(copy, data, xi_data_sizes[type]);
   }

   struct xi_data *xi_data;
   if ((xi_data = chckIterPoolAdd(archive->data, &xi_data, NULL))) {
      xi_data->type = type;
      xi_data->any = copy;
   } else {
      free(copy);
   }

   return (xi_data != NULL);
}

struct xi_archive*
xi_archive_new(void)
{
   struct xi_archive *archive;

   if (!(archive = calloc(1, sizeof(struct xi_archive))))
      goto fail;

   if (!(archive->data = chckIterPoolNew(30, 0, sizeof(struct xi_data))))
      goto fail;

   return archive;

fail:
   if (archive)
      xi_archive_free(archive);
   return NULL;
}

void
xi_archive_free(struct xi_archive *archive)
{
   assert(archive);

   if (archive->data) {
      chckIterPoolIterCall(archive->data, data_free);
      chckIterPoolFree(archive->data);
   }

   free(archive);
}

static void
read_string(chckBuffer *buf, char out_bytes[1024], size_t *out_length)
{
   assert(buf && out_bytes && out_length);

   *out_length = 0;
   size_t max_size = chckBufferGetSize(buf) - chckBufferGetOffset(buf);
   max_size = MIN(max_size, 1024);

   // 1024 bytes should be enough to hold all the strings
   // strings are aligned by 4 bytes
   for (size_t p = 0; p + 4 <= max_size && chckBufferRead(out_bytes + p, 1, 4, buf) == 4; p += 4) {
      // check for \0 terminator, and increase lenght until then.
      for (char *b = out_bytes + p; b - (out_bytes + p) < 4; ++b) {
         if (*b == 0)
            return;

         *out_length += 1;
      }
   }
}

static struct xi_string*
read_strings(chckBuffer *buf, uint32_t *out_num_strings)
{
   assert(buf && out_num_strings);

   *out_num_strings = 0;

   size_t offset = chckBufferGetOffset(buf);
   struct xi_string *strings = NULL;
   uint32_t *offsets = NULL;

   uint32_t num_strings;
   chckBufferReadUInt32(buf, &num_strings);

   if (!(offsets = calloc(num_strings, sizeof(uint32_t))))
      goto fail;

   if (!(strings = calloc(num_strings, sizeof(struct xi_string))))
      goto fail;

   for (uint32_t i = 0; i < num_strings; ++i) {
      chckBufferReadUInt32(buf, &offsets[i]);
      chckBufferReadUInt32(buf, &strings[i].flags);
   }

   for (uint32_t i = 0; i < num_strings; ++i) {
      chckBufferSeek(buf, offset + offsets[i], SEEK_SET);

      uint32_t indicator;
      chckBufferReadUInt32(buf, &indicator);

      if (indicator != 1)
         continue;

      // uint32_t padding[6] (0)
      chckBufferSeek(buf, sizeof(uint32_t) * 6, SEEK_CUR);

      char bytes[1024];
      read_string(buf, bytes, &strings[i].length);

      if (!(strings[i].data = calloc(1, strings[i].length + 1)))
         continue;

      memcpy(strings[i].data, bytes, strings[i].length);
   }

   free(offsets);
   *out_num_strings = num_strings;
   return strings;

fail:
   if (offsets)
      free(offsets);
   if (strings) {
      for (uint32_t i = 0; i < num_strings; ++i)
         free(strings[i].data);
      free(strings);
   }
   return NULL;
}

static bool
detect_name_id(chckBuffer *buf)
{
   return (chckBufferGetSize(buf) >= 32 && !memcmp(chckBufferGetPointer(buf), "none\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 32));
}

static void
parse_name_id(struct xi_archive *archive, chckBuffer *buf)
{
   assert(archive && buf);

   struct xi_name_id name_id;
   while (chckBufferRead(&name_id.name, 1, sizeof(name_id.name), buf) == sizeof(name_id.name) && chckBufferReadUInt32(buf, &name_id.id))
      archive_add_data(archive, XI_TYPE_NAME_ID, &name_id);
}

static bool
detect_ability(chckBuffer *buf)
{
   struct xi_ability ability;

   if (chckBufferGetSize(buf) < 0x400)
      return false;

   memcpy(&ability, chckBufferGetPointer(buf), sizeof(ability));
   decode((uint8_t*)&ability, sizeof(ability), rotation_for_variable_encryption((uint8_t*)&ability, sizeof(ability)));
   return (ability.index == 0 && ability.icon_id == 11776 && ability.mp_cost == 0 && ability.targets == 1 && ability.name[0] == '.' && ability.description[0] == '.');
}

static void
parse_ability(struct xi_archive *archive, chckBuffer *buf)
{
   assert(archive && buf);

   uint8_t *data = (uint8_t*)chckBufferGetOffsetPointer(buf);
   decode(data, 0x400, rotation_for_variable_encryption(data, 0x400));

   struct xi_ability ability;
   while (chckBufferReadUInt16(buf, &ability.index) &&
          chckBufferReadUInt16(buf, &ability.icon_id) &&
          chckBufferReadUInt16(buf, &ability.mp_cost) &&
          chckBufferReadUInt16(buf, &ability.unknown) &&
          chckBufferReadUInt16(buf, &ability.targets) &&
          chckBufferRead(ability.name, 1, sizeof(ability.name), buf) == sizeof(ability.name) &&
          chckBufferRead(ability.description, 1, sizeof(ability.description), buf) == sizeof(ability.description)) {
      archive_add_data(archive, XI_TYPE_ABILITY, &ability);
      chckBufferSeek(buf, 0x2D6, SEEK_CUR);

      if (chckBufferGetOffset(buf) + 0x400 >= chckBufferGetSize(buf))
         break;

      data = (uint8_t*)chckBufferGetOffsetPointer(buf);
      decode(data, 0x400, rotation_for_variable_encryption(data, 0x400));
   }
}

static bool
detect_spell(chckBuffer *buf)
{
   struct xi_spell spell;

   if (chckBufferGetSize(buf) < 0x400)
      return false;

   memcpy(&spell, chckBufferGetPointer(buf), sizeof(spell));
   decode((uint8_t*)&spell, sizeof(spell), rotation_for_variable_encryption((uint8_t*)&spell, sizeof(spell)));
   return (spell.index == 0 && spell.type == 0 && spell.element == 6 && spell.targets == 63 && spell.skill == 32 && spell.mp_cost == 0);
}

static void
parse_spell(struct xi_archive *archive, chckBuffer *buf)
{
   assert(archive && buf);

   uint8_t *data = (uint8_t*)chckBufferGetOffsetPointer(buf);
   decode(data, 0x400, rotation_for_variable_encryption(data, 0x400));

   struct xi_spell spell;
   while (chckBufferReadUInt16(buf, &spell.index) &&
          chckBufferReadUInt16(buf, &spell.type) &&
          chckBufferReadUInt16(buf, &spell.element) &&
          chckBufferReadUInt16(buf, &spell.targets) &&
          chckBufferReadUInt16(buf, &spell.skill) &&
          chckBufferReadUInt16(buf, &spell.mp_cost) &&
          chckBufferReadUInt8(buf, &spell.casting_time) &&
          chckBufferReadUInt8(buf, &spell.recast_delay) &&
          chckBufferRead(spell.level, 1, sizeof(spell.level), buf) == sizeof(spell.level) &&
          chckBufferReadUInt16(buf, &spell.id) &&
          chckBufferReadUInt8(buf, &spell.unknown) &&
          chckBufferRead(spell.jp_name, 1, sizeof(spell.jp_name), buf) == sizeof(spell.jp_name) &&
          chckBufferRead(spell.en_name, 1, sizeof(spell.en_name), buf) == sizeof(spell.en_name) &&
          chckBufferRead(spell.jp_description, 1, sizeof(spell.jp_description), buf) == sizeof(spell.jp_description) &&
          chckBufferRead(spell.en_description, 1, sizeof(spell.en_description), buf) == sizeof(spell.en_description)) {
      archive_add_data(archive, XI_TYPE_SPELL, &spell);
      chckBufferSeek(buf, 0x2AF, SEEK_CUR);

      if (chckBufferGetOffset(buf) + 0x400 >= chckBufferGetSize(buf))
         break;

      data = (uint8_t*)chckBufferGetOffsetPointer(buf);
      decode(data, 0x400, rotation_for_variable_encryption(data, 0x400));
   }
}

static bool
detect_item(chckBuffer *buf)
{
   struct xi_item item;

   if (chckBufferGetSize(buf) < sizeof(item))
      return false;

   memcpy(&item, chckBufferGetPointer(buf), sizeof(item));
   decode((uint8_t*)&item, sizeof(item), 5);
   return (item.id > 0 && item.type != XI_ITEM_TYPE_NONE);
}

static void
parse_item(struct xi_archive *archive, chckBuffer *buf)
{
   assert(archive && buf);

   struct xi_item item;
   memset(&item, 0, sizeof(item));

   while (chckBufferReadUInt32(buf, &item.id) &&
          chckBufferReadUInt16(buf, &item.flags) &&
          chckBufferReadUInt16(buf, &item.stack) &&
          chckBufferReadUInt16(buf, &item.type) &&
          chckBufferReadUInt16(buf, &item.resource) &&
          chckBufferReadUInt16(buf, &item.targets)) {
      assert(item.id > 0 && item.type != XI_ITEM_TYPE_NONE);
      size_t next = chckBufferGetOffset(buf) + 0x202 + 0xA00 - 16;

      if (item.type == XI_ITEM_TYPE_WEAPON) {
         struct xi_item_weapon weapon;
         chckBufferReadUInt16(buf, &weapon.level);
         chckBufferReadUInt16(buf, &weapon.slots);
         chckBufferReadUInt16(buf, &weapon.races);
         chckBufferReadUInt32(buf, &weapon.jobs);
         chckBufferReadUInt16(buf, &weapon.damage);
         chckBufferReadUInt16(buf, &weapon.delay);
         chckBufferReadUInt16(buf, &weapon.dps);
         chckBufferReadUInt8(buf, &weapon.skill);
         chckBufferReadUInt8(buf, &weapon.jug_size);
         chckBufferReadUInt32(buf, &weapon.unknown);
         chckBufferReadUInt8(buf, &weapon.max_charges);
         chckBufferReadUInt8(buf, &weapon.casting_time);
         chckBufferReadUInt16(buf, &weapon.use_delay);
         chckBufferReadUInt32(buf, &weapon.reuse_delay);
         chckBufferReadUInt32(buf, &weapon.unknown2);
         item_set_data(&item, sizeof(weapon), &weapon);
      } else if (item.type == XI_ITEM_TYPE_ARMOR) {
         struct xi_item_armor armor;
         chckBufferReadUInt16(buf, &armor.level);
         chckBufferReadUInt16(buf, &armor.slots);
         chckBufferReadUInt16(buf, &armor.races);
         chckBufferReadUInt32(buf, &armor.jobs);
         chckBufferReadUInt16(buf, &armor.shield_size);
         chckBufferReadUInt8(buf, &armor.max_charges);
         chckBufferReadUInt8(buf, &armor.casting_time);
         chckBufferReadUInt16(buf, &armor.use_delay);
         chckBufferReadUInt16(buf, &armor.unknown);
         chckBufferReadUInt32(buf, &armor.reuse_delay);
         chckBufferReadUInt32(buf, &armor.unknown2);
         item_set_data(&item, sizeof(armor), &armor);
      } else if (item.type == XI_ITEM_TYPE_PUPPET) {
         struct xi_item_puppet puppet;
         chckBufferReadUInt16(buf, &puppet.slot);
         chckBufferReadUInt32(buf, &puppet.element_charge);
         chckBufferReadUInt32(buf, &puppet.unknown);
         item_set_data(&item, sizeof(puppet), &puppet);
      } else if (item.type == XI_ITEM_TYPE_FURNISHING || item.type == XI_ITEM_TYPE_MANNEQUIN || item.type == XI_ITEM_TYPE_FLOWERPOT) {
         struct xi_item_general general;
         chckBufferReadUInt16(buf, &general.element);
         chckBufferReadUInt32(buf, &general.storage_slots);
         item_set_data(&item, sizeof(general), &general);
      } else if (item.flags & XI_ITEM_USABLE) {
         struct xi_item_usable usable;
         chckBufferReadUInt16(buf, &usable.activation_time);
         chckBufferReadUInt32(buf, &usable.unknown);
         chckBufferReadUInt32(buf, &usable.unknown2);
         item_set_data(&item, sizeof(usable), &usable);
      }

      item.strings = read_strings(buf, &item.num_strings);
      archive_add_data(archive, XI_TYPE_ITEM, &item);
      memset(&item, 0, sizeof(item));

      chckBufferSeek(buf, next, SEEK_SET);
   }
}

struct xi_archive*
xi_archive_load_from_memory(const void *data, const size_t size)
{
   assert(data && size);

   struct xi_archive *archive;
   chckBuffer *buf = NULL;

   if (!(archive = xi_archive_new()))
      goto fail;

   if (!(buf = chckBufferNewFromPointer(data, size, CHCK_BUFFER_ENDIAN_LITTLE)))
      goto fail;

   struct header {
      const char *data;
      size_t size;
   };

   static struct {
      bool (*detect)(chckBuffer *buf);
      void (*parse)(struct xi_archive *archive, chckBuffer *buf);
      int fixed_encryption; // 0 == none, > 0 number of bits to rotate right
   } map[XI_TYPE_UNKNOWN] = {
      { // XI_TYPE_NAME_ID
         .detect = detect_name_id,
         .parse = parse_name_id,
         .fixed_encryption = 0,
      },
      { // XI_TYPE_ABILITY
         .detect = detect_ability,
         .parse = parse_ability,
         .fixed_encryption = 0, // has variable encryption
      },
      { // XI_TYPE_SPELL
         .detect = detect_spell,
         .parse = parse_spell,
         .fixed_encryption = 0, // has variable encryption
      },
      { // XI_TYPE_ITEM
         .detect = detect_item,
         .parse = parse_item,
         .fixed_encryption = 5,
      },
   };

   bool found = false;
   for (unsigned int i = 0; i < XI_TYPE_UNKNOWN; ++i) {
      if (!map[i].detect(buf))
         continue;

      if (map[i].fixed_encryption > 0) {
         decode((uint8_t*)data, size, map[i].fixed_encryption);
#if 0
         FILE *f = fopen("dec.dat", "wb");
         fwrite(data, 1, size, f);
         fclose(f);
#endif
      }

      map[i].parse(archive, buf);
      found = true;
      break;
   }

   if (!found)
      archive_add_data(archive, XI_TYPE_UNKNOWN, NULL);

   chckBufferFree(buf);
   return archive;

fail:
   if (archive)
      xi_archive_free(archive);
   if (buf)
      chckBufferFree(buf);
   return NULL;
}

struct xi_archive*
xi_archive_load_from_file(const char *file)
{
   assert(file);

   void *data;
   size_t size;
   if (!(data = data_from_file(file, &size)))
      goto fail;

   struct xi_archive *archive = xi_archive_load_from_memory(data, size);
   free(data);
   return archive;

fail:
   free(data);
   return NULL;
}

const struct xi_data*
xi_archive_get_data_list(struct xi_archive *archive, size_t *out_count)
{
   assert(archive);
   return chckIterPoolToCArray(archive->data, out_count);
}

struct xi_ftable*
xi_ftable_new(void)
{
   struct xi_ftable *ftable;

   if (!(ftable = calloc(1, sizeof(struct xi_ftable))))
      goto fail;

   if (!(ftable->data = chckIterPoolNew(30, 0, sizeof(struct xi_file_entry))))
      goto fail;

   return ftable;

fail:
   if (ftable)
      xi_ftable_free(ftable);
   return NULL;
}

void
xi_ftable_free(struct xi_ftable *ftable)
{
   assert(ftable);

   if (ftable->data)
      chckIterPoolFree(ftable->data);

   free(ftable);
}

struct xi_ftable*
xi_ftable_load_from_memory(const void *f_data, const size_t f_size, const void *v_data, const size_t v_size)
{
   assert(f_data && f_size && v_data && v_size);

   struct xi_ftable *ftable;
   chckBuffer *buf[2] = { NULL, NULL };

   if (!(ftable = xi_ftable_new()))
      goto fail;

   if (!(buf[0] = chckBufferNewFromPointer(f_data, f_size, CHCK_BUFFER_ENDIAN_LITTLE)) ||
       !(buf[1] = chckBufferNewFromPointer(v_data, v_size, CHCK_BUFFER_ENDIAN_LITTLE)))
      goto fail;

   struct xi_file_entry entry;
   while (chckBufferReadUInt16(buf[0], &entry.id)) {
      chckBufferReadUInt8(buf[1], &entry.exist);
      chckIterPoolAdd(ftable->data, &entry, NULL);
   }

   for (int i = 0; i < 2; ++i)
      chckBufferFree(buf[i]);
   return ftable;

fail:
   if (ftable)
      xi_ftable_free(ftable);
   for (int i = 0; i < 2; ++i)
      if (buf[i])
         chckBufferFree(buf[i]);
   return NULL;
}

const struct xi_ftable*
xi_ftable_load_from_file(const char *f_ftable, const char *f_vtable)
{
   assert(f_ftable && f_vtable);

   size_t size[2];
   void *data[2] = { NULL, NULL };
   if (!(data[0] = data_from_file(f_ftable, &size[0])) ||
       !(data[1] = data_from_file(f_vtable, &size[1])))
      goto fail;

   struct xi_ftable *ftable = xi_ftable_load_from_memory(data[0], size[0], data[1], size[1]);
   for (int i = 0; i < 2; ++i)
      free(data[i]);
   return ftable;

fail:
   for (int i = 0; i < 2; ++i)
      free(data[i]);
   return NULL;
}
