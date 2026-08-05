with open("code/renderercommon/tr_image_pvr.c", "r") as f:
    content = f.read()

old_str = """	// clean up
	ri.FS_FreeFile(buffer);

	// something failed
	if (!ret)
		return;

	// return stuff
	*pic = ret;
	if (width)
		*width = pvr->width;
	if (height)
		*height = pvr->height;
}"""

new_str = """	// clean up
	ri.FS_FreeFile(buffer);

	// something failed
	if (!ret)
		return;

	// return stuff
	*pic = ret;
	if (width)
		*width = pvr->width;
	if (height)
		*height = pvr->height;

fail:
	if (!*pic && buffer) ri.FS_FreeFile(buffer);
}"""

if old_str in content:
    content = content.replace(old_str, new_str)
    with open("code/renderercommon/tr_image_pvr.c", "w") as f:
        f.write(content)
    print("Replaced!")
else:
    print("Not found!")
