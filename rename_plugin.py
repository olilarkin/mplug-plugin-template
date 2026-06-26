#!/usr/bin/env python3
"""Rename this template into a new plugin project.

Run once, right after creating a repo from the template. It renames the C++
class, source files, plugin identifier, AudioUnit subtype and app name across
CMakeLists.txt and src/.

Usage:
  python rename_plugin.py <NewClassName> <Manufacturer> [options]

Example:
  python rename_plugin.py AcmeFilter "Acme Audio"
  python rename_plugin.py AcmeFilter "Acme Audio" --id com.acme.filter --subtype acfl

Arguments:
  NewClassName   CamelCase C++ class name (e.g. AcmeFilter). Also used for the
                 CMake project name and target prefix.
  Manufacturer   Vendor/company name (e.g. "Acme Audio"). Used to derive the
                 default plugin id.

Options:
  --id ID            Reverse-domain plugin id (default: com.<vendor>.<class>).
  --subtype CODE     4-character AudioUnit subtype (default: derived).
  -h, --help         Show this help.
"""

import argparse
import os
import re
import sys

OLD_CLASS = "MyPlugin"
OLD_SNAKE = "my_plugin"
OLD_ID = "com.example.myplugin"
OLD_SUBTYPE = "mypl"

# Files/dirs we never touch.
SKIP_DIRS = {".git", "build", "__pycache__"}
# Only rewrite these text file types.
TEXT_EXTS = {".txt", ".json", ".h", ".hpp", ".cpp", ".mm", ".c", ".md", ".yml", ".yaml", ".py"}


def camel_to_snake(name: str) -> str:
    s1 = re.sub(r"(.)([A-Z][a-z]+)", r"\1_\2", name)
    s1 = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", s1)
    return s1.lower()


def derive_subtype(class_name: str) -> str:
    letters = re.sub(r"[^A-Za-z0-9]", "", class_name)
    code = (letters + "xxxx")[:4]
    # AU subtypes conventionally start with an uppercase letter.
    return code[0].upper() + code[1:].lower()


def main() -> int:
    parser = argparse.ArgumentParser(add_help=True, description="Rename the MPlug plugin template.")
    parser.add_argument("new_class", help="CamelCase C++ class name, e.g. AcmeFilter")
    parser.add_argument("manufacturer", help='Vendor name, e.g. "Acme Audio"')
    parser.add_argument("--id", dest="plugin_id", default=None)
    parser.add_argument("--subtype", dest="subtype", default=None)
    args = parser.parse_args()

    new_class = args.new_class
    if not re.match(r"^[A-Za-z][A-Za-z0-9]*$", new_class):
        print("error: NewClassName must be alphanumeric and start with a letter", file=sys.stderr)
        return 1

    new_snake = camel_to_snake(new_class)
    vendor_slug = re.sub(r"[^a-z0-9]", "", args.manufacturer.lower())
    new_id = args.plugin_id or f"com.{vendor_slug}.{new_class.lower()}"
    new_subtype = args.subtype or derive_subtype(new_class)

    if len(new_subtype) != 4:
        print("error: --subtype must be exactly 4 characters", file=sys.stderr)
        return 1

    # Order matters: replace the most specific tokens first.
    replacements = [
        (OLD_ID, new_id),
        ('"' + OLD_SUBTYPE + '"', '"' + new_subtype + '"'),
        (OLD_CLASS, new_class),
        (OLD_SNAKE, new_snake),
    ]

    root = os.path.dirname(os.path.abspath(__file__))
    changed_files = 0
    renamed_files = 0

    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        for fname in filenames:
            if fname == os.path.basename(__file__):
                continue
            ext = os.path.splitext(fname)[1]
            if ext not in TEXT_EXTS:
                continue
            fpath = os.path.join(dirpath, fname)
            try:
                with open(fpath, "r", encoding="utf-8") as f:
                    content = f.read()
            except (UnicodeDecodeError, OSError):
                continue
            new_content = content
            for old, new in replacements:
                new_content = new_content.replace(old, new)
            if new_content != content:
                with open(fpath, "w", encoding="utf-8") as f:
                    f.write(new_content)
                changed_files += 1

    # Rename source files my_plugin* -> <new_snake>*
    src_dir = os.path.join(root, "src")
    if os.path.isdir(src_dir):
        for fname in os.listdir(src_dir):
            if fname.startswith(OLD_SNAKE):
                new_name = new_snake + fname[len(OLD_SNAKE):]
                os.rename(os.path.join(src_dir, fname), os.path.join(src_dir, new_name))
                renamed_files += 1

    print(f"Renamed project to '{new_class}'")
    print(f"  class      : {new_class}")
    print(f"  files base : {new_snake}")
    print(f"  plugin id  : {new_id}")
    print(f"  AU subtype : {new_subtype}")
    print(f"  updated {changed_files} file(s), renamed {renamed_files} source file(s)")
    print("\nReview the changes, then configure and build. You can delete rename_plugin.py afterwards.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
