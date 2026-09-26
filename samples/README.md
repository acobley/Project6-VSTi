# Factory samples

Everything in this folder that is a `.wav`, `.mid` or `.midi` file ships in
the installer as the **Factory samples** component, and is installed to

    /Users/Shared/Project6/Samples

keeping its sub-folders. This README and anything else is left out.

## Making a sample project that travels

1. Put the files here, in sub-folders if you like (`Drums/`, `Keys/`, …).
2. Copy them to `/Users/Shared/Project6/Samples` on your own Mac, with the same
   sub-folders — or build and run the installer, which does it for you.
3. Build the project by dragging the pads **from `/Users/Shared/Project6/Samples`**,
   not from here. A pad remembers the path it was loaded from, and only that
   path is the same on every Mac.

Pads loaded from the factory folder are **not** embedded in the project: every
recipient who installs Project6 already has them. Any other file on a pad
**is** embedded, so a project can also be sent to someone who has only the
plug-in. See `PORTING-NOTES.md`, "Projects that travel".
