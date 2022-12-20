#include "cinder.h"

#include <stdlib.h>
#include <string.h>

static void object(CinderMachineObject *machine) {
    cinder_machine_init(machine);
    size_t index = cinder_debug_section(machine, ".debug_probe", 1U);
    for (unsigned i = 0U; i < 32U; ++i) cinder_bytes_put8(&machine->debug_sections.data[index].bytes, 0U);
    CinderFixup fix = {0U, cinder_strndup("external", 8U), 1, 0, 4U};
    cinder_vec_push((CinderVec *)&machine->fixups, &fix);
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    const char sentinel[] = "previous complete object\n";
    FILE *file = fopen(argv[1], "wb");
    if (file == NULL || fwrite(sentinel, 1U, sizeof(sentinel), file) != sizeof(sentinel) || fclose(file) != 0) return 2;
    for (unsigned mutation = 0U; mutation < 12U; ++mutation) {
        CinderMachineObject machine; object(&machine);
        CinderDebugSection *section = &machine.debug_sections.data[0];
        switch (mutation) {
            case 0U: free(section->name); section->name = cinder_strndup(".debug_bad\n.text", 16U); break;
            case 1U: {
                size_t added = cinder_debug_section(&machine, ".debug_other", 1U);
                free(machine.debug_sections.data[added].name);
                machine.debug_sections.data[added].name = cinder_strndup(".debug_probe", 12U); break;
            }
            case 2U: section->alignment = 3U; break;
            case 3U: section->bytes.len = (size_t)UINT32_MAX + 1U; break;
            case 4U: {
                for (unsigned n = 0U; n < 16U; ++n) {
                    char name[32]; (void)snprintf(name, sizeof(name), ".debug_extra%u", n);
                    (void)cinder_debug_section(&machine, name, 1U);
                }
                break;
            }
            case 5U: free(machine.fixups.data[0].symbol); machine.fixups.data[0].symbol = NULL; break;
            case 6U: machine.fixups.data[0].section_kind = 3U; break;
            case 7U: machine.fixups.data[0].section_kind = 99U; break;
            case 8U: machine.fixups.data[0].type = 4; break;
            case 9U: machine.fixups.data[0].offset = 31U; machine.fixups.data[0].type = 10; break;
            case 10U: machine.fixups.data[0].offset = 25U; break;
            case 11U: {
                CinderFixup overlap = {4U, cinder_strndup("external", 8U), 10, 0, 4U};
                cinder_vec_push((CinderVec *)&machine.fixups, &overlap); break;
            }
            default: return 2;
        }
        CinderDiagnostics diags; cinder_diags_init(&diags);
        if (cinder_write_elf64(&machine, argv[1], &diags) == 0 || diags.errors == 0U) return 1;
        cinder_diags_destroy(&diags); cinder_diags_init(&diags);
        FILE *assembly = tmpfile();
        if (assembly == NULL || cinder_write_assembly(&machine, assembly, &diags) == 0 || diags.errors == 0U || ftell(assembly) != 0L) return 1;
        (void)fclose(assembly); cinder_diags_destroy(&diags);
        file = fopen(argv[1], "rb"); char previous[sizeof(sentinel)];
        if (file == NULL || fread(previous, 1U, sizeof(previous), file) != sizeof(previous) || fgetc(file) != EOF || memcmp(previous, sentinel, sizeof(sentinel)) != 0) return 1;
        (void)fclose(file); cinder_machine_destroy(&machine);
    }
    puts("Unwind output: twelve malformed section/relocation records rejected; prior output preserved");
    return 0;
}
