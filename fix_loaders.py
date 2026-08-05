import re
import glob

def patch_loader(filepath, func_name):
    with open(filepath, "r") as f:
        content = f.read()

    # Replace ri.Error( ERR_DROP, ...) with ri.Printf(PRINT_WARNING, ...) and goto fail;
    def repl(m):
        args = m.group(1)
        return f'{{ ri.Printf( PRINT_WARNING, {args} ); goto fail; }}'

    new_content = re.sub(r'ri\.Error\s*\(\s*ERR_DROP\s*,\s*(.*?)\s*\);', repl, content, flags=re.DOTALL)
    
    # Insert the fail label before the last return
    # Find the end of the function. We know R_LoadBMP and R_LoadTGA have '*pic = ...' at the end.
    if func_name == "R_LoadBMP":
        # in tr_image_bmp.c
        new_content = new_content.replace("\tbmpRGBA = ri.Malloc( numPixels * 4 );\n\t*pic = bmpRGBA;", 
            "fail:\n\tif (!*pic && buffer.b) ri.FS_FreeFile(buffer.v);\n\tif (!*pic) return;\n\tbmpRGBA = ri.Malloc( numPixels * 4 );\n\t*pic = bmpRGBA;")
    elif func_name == "R_LoadTGA":
        # in tr_image_tga.c
        new_content = new_content.replace("\t*pic = pic32;\n\n}", 
            "\t*pic = pic32;\n\nfail:\n\tif (!*pic && buffer.b) ri.FS_FreeFile(buffer.v);\n}")
    
    with open(filepath, "w") as f:
        f.write(new_content)

patch_loader("code/renderercommon/tr_image_bmp.c", "R_LoadBMP")
patch_loader("code/renderercommon/tr_image_tga.c", "R_LoadTGA")
patch_loader("code/renderercommon/tr_image_pcx.c", "R_LoadPCX")
patch_loader("code/renderercommon/tr_image_pvr.c", "R_LoadPVR")
