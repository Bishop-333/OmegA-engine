with open("code/renderercommon/tr_image_pcx.c", "r") as f:
    content = f.read()

old_str = """	*pic = out;

	ri.FS_FreeFile (pcx);
	ri.Free (pic8);
}"""

new_str = """	*pic = out;

fail:
	if (!*pic && pcx) ri.FS_FreeFile(pcx);
	if (*pic) ri.FS_FreeFile (pcx);
	if (pic8) ri.Free (pic8);
}"""

if old_str in content:
    content = content.replace(old_str, new_str)
    with open("code/renderercommon/tr_image_pcx.c", "w") as f:
        f.write(content)
    print("Replaced!")
else:
    print("Not found!")
