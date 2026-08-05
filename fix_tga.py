with open("code/renderercommon/tr_image_tga.c", "r") as f:
    content = f.read()

old_str = """  *pic = targa_rgba;

  ri.FS_FreeFile (buffer.v);
}"""

new_str = """  *pic = targa_rgba;

fail:
  if (!*pic && buffer.b) ri.FS_FreeFile(buffer.v);
  if (*pic) ri.FS_FreeFile (buffer.v);
}"""

if old_str in content:
    content = content.replace(old_str, new_str)
    with open("code/renderercommon/tr_image_tga.c", "w") as f:
        f.write(content)
    print("Replaced!")
else:
    print("Not found!")
