#include <stdlib.h>
#include <stdio.h>
#include "xi.h"

int
main(int argc, char **argv)
{
   if (argc < 2) {
      fprintf(stderr, "Supply some dat file paths as argument.\n");
      return EXIT_FAILURE;
   }

   // xi_ftable_load_from_file(argv[1], argv[2]);

   for (int f = 1; f < argc; ++f) {
      struct xi_archive *archive;
      if (!(archive = xi_archive_load_from_file(argv[f]))) {
         fprintf(stderr, "Could not load archive: %s\n", argv[f]);
         continue;
      }

      size_t count;
      const struct xi_data *data = xi_archive_get_data_list(archive, &count);
      for (size_t i = 0; i < count; ++i) {
         switch (data->type) {
            case XI_TYPE_NAME_ID: {
                  struct xi_name_id *name_id = data[i].name_id;
                  printf("%8d: %s\n", name_id->id, name_id->name);
               }
               break;

            case XI_TYPE_ABILITY: {
                  struct xi_ability *ability = data[i].ability;
                  printf("%sIndex: %u\n", (i > 0 ? "\n" : ""), ability->index);
                  printf("Icon ID: %u\n", ability->icon_id);
                  printf("MP Cost: %u\n", ability->mp_cost);
                  printf("Targets: %u\n", ability->targets);

                  printf("--- Strings ---\n");
                  printf("%s\n", ability->name);
                  printf("%s\n", ability->description);
                  printf("---------------\n");
            }
            break;

            case XI_TYPE_SPELL: {
                  struct xi_spell *spell = data[i].spell;
                  printf("%sIndex: %u\n", (i > 0 ? "\n" : ""), spell->index);
                  printf("Type: %u\n", spell->type);
                  printf("Element: %u\n", spell->element);
                  printf("Targets: %u\n", spell->targets);
                  printf("Skill: %u\n", spell->skill);
                  printf("MP Cost: %u\n", spell->mp_cost);

                  printf("--- Strings ---\n");
                  printf("%s\n", spell->en_name);
                  printf("%s\n", spell->en_description);
                  printf("---------------\n");
            }
            break;

            case XI_TYPE_ITEM: {
                  struct xi_item *item = data[i].item;
                  printf("%sID: %u\n", (i > 0 ? "\n" : ""), item->id);
                  printf("Flags: %u\n", item->flags);
                  printf("Stack: %u\n", item->stack);
                  printf("Type: %u\n", item->type);
                  printf("Resource: %u Targets: %u\n", item->resource, item->targets);

                  printf("--- Strings ---\n");
                  for (uint32_t s = 0; s < item->num_strings; ++s)
                     printf("%d. %s\n", s, item->strings[s].data);
                  printf("---------------\n");
               }
               break;

            default:
               puts("unknown data");
               break;
         }
      }

      xi_archive_free(archive);
   }

   return EXIT_SUCCESS;
}
