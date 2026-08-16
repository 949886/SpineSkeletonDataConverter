import argparse
import shutil
import subprocess
import sys
from pathlib import Path


def format_command(command: list[str]) -> str:
	return subprocess.list2cmdline(command)


def locate_executable(name: str) -> Path:
	script_dir = Path(__file__).resolve().parent
	direct_candidate = script_dir / name
	if direct_candidate.exists():
		return direct_candidate

	for candidate in script_dir.rglob(name):
		if candidate.is_file():
			return candidate

	raise FileNotFoundError(
		f"Could not locate {name} under {script_dir}. Ensure the executable is placed alongside this script."
	)


def determine_output_suffix(source_suffix: str, format_option: str) -> str:
	source_suffix = source_suffix.lower()
	if format_option == "same":
		return source_suffix
	if format_option == "json":
		return ".json"
	if format_option == "skel":
		return ".skel"
	if format_option == "other":
		if source_suffix == ".json":
			return ".skel"
		if source_suffix == ".skel":
			return ".json"
		raise ValueError("--format other only applies to .json or .skel files")
	raise ValueError(f"Unsupported format option: {format_option}")


def build_converter_command(executable: Path, input_path: Path, output_path: Path, version: str | None, remove_curve: bool) -> list[str]:
	command = [str(executable), str(input_path), str(output_path)]
	if version:
		command.extend(["-v", version])
	if remove_curve:
		command.append("--remove-curve")
	return command


def build_atlas_command(executable: Path, input_path: Path, output_dir: Path) -> list[str]:
	return [str(executable), str(input_path), str(output_dir)]


def is_spine_43_target(version: str | None) -> bool:
	if not version:
		return False
	parts = version.split(".")
	if len(parts) != 3:
		return False
	try:
		return (int(parts[0]), int(parts[1])) >= (4, 3)
	except ValueError:
		return False


def parse_atlas_entry(line: str) -> tuple[str, list[str]] | None:
	if ":" not in line:
		return None
	key, raw_value = line.split(":", 1)
	key = key.strip()
	if not key:
		return None
	return key, [value.strip() for value in raw_value.split(",")]


def parse_atlas(text: str) -> dict:
	"""Parse the line-oriented Spine atlas format without interpreting image data."""
	text = text.lstrip("\ufeff")
	blocks: list[list[str]] = []
	current: list[str] = []

	for raw_line in text.splitlines():
		line = raw_line.strip()
		if not line:
			if current:
				blocks.append(current)
				current = []
			continue
		current.append(line)
	if current:
		blocks.append(current)

	if not blocks:
		raise ValueError("Atlas is empty")

	header: list[tuple[str, list[str]]] = []
	pages: list[dict] = []

	for block_index, block in enumerate(blocks):
		i = 0
		if block_index == 0:
			while i < len(block):
				entry = parse_atlas_entry(block[i])
				if entry is None:
					break
				header.append(entry)
				i += 1

		if i >= len(block):
			raise ValueError(f"Atlas block {block_index + 1} contains metadata but no page name")

		page_name = block[i]
		if parse_atlas_entry(page_name) is not None:
			raise ValueError(f"Invalid atlas page name: {page_name!r}")
		i += 1

		page_entries: list[tuple[str, list[str]]] = []
		while i < len(block):
			entry = parse_atlas_entry(block[i])
			if entry is None:
				break
			page_entries.append(entry)
			i += 1

		regions: list[dict] = []
		while i < len(block):
			region_name = block[i]
			if parse_atlas_entry(region_name) is not None:
				raise ValueError(f"Expected atlas region name, got metadata line: {region_name!r}")
			i += 1
			region_entries: list[tuple[str, list[str]]] = []
			while i < len(block):
				entry = parse_atlas_entry(block[i])
				if entry is None:
					break
				region_entries.append(entry)
				i += 1
			regions.append({"name": region_name, "entries": region_entries})

		pages.append({"name": page_name, "entries": page_entries, "regions": regions})

	return {"header": header, "pages": pages}


def atlas_entries_to_dict(entries: list[tuple[str, list[str]]]) -> dict[str, list[str]]:
	return {key: values for key, values in entries}


def compact_page_entries(entries: list[tuple[str, list[str]]]) -> list[tuple[str, list[str]]]:
	"""Use Spine 4.3-style page metadata while retaining non-default semantics."""
	result: list[tuple[str, list[str]]] = []
	for key, values in entries:
		lower = key.lower()
		if lower == "format" and values == ["RGBA8888"]:
			# Spine 4.3 exporter omits the default RGBA8888 format.
			continue
		if lower == "repeat" and len(values) == 1 and values[0].lower() == "none":
			# Clamp-to-edge is the runtime default and is omitted by 4.3 exports.
			continue
		if lower == "pma" and len(values) == 1 and values[0].lower() == "false":
			# Do not invent PMA; false is already the parser default.
			continue
		result.append((key, values))
	return result


def normalize_rotate(values: list[str]) -> list[str] | None:
	if not values:
		return None
	value = values[0].strip().lower()
	if value in {"false", "0"}:
		return None
	if value == "true":
		return ["90"]
	return [values[0].strip()]


def compact_region_entries(region_name: str, entries: list[tuple[str, list[str]]]) -> list[tuple[str, list[str]]]:
	by_key = atlas_entries_to_dict(entries)

	bounds = by_key.get("bounds")
	legacy_size = by_key.get("size")
	if bounds is None:
		xy = by_key.get("xy")
		if xy is None or legacy_size is None or len(xy) < 2 or len(legacy_size) < 2:
			raise ValueError(f"Atlas region '{region_name}' has neither bounds nor a complete xy/size pair")
		bounds = [xy[0], xy[1], legacy_size[0], legacy_size[1]]
	if len(bounds) < 4:
		raise ValueError(f"Atlas region '{region_name}' has invalid bounds: {bounds}")

	result: list[tuple[str, list[str]]] = [("bounds", bounds[:4])]

	offsets = by_key.get("offsets")
	if offsets is None:
		offset = by_key.get("offset")
		orig = by_key.get("orig")
		if offset is not None and orig is not None and len(offset) >= 2 and len(orig) >= 2:
			packed_size = legacy_size if legacy_size is not None else bounds[2:4]
			is_default = (
				offset[0] == "0"
				and offset[1] == "0"
				and len(packed_size) >= 2
				and orig[0] == packed_size[0]
				and orig[1] == packed_size[1]
			)
			if not is_default:
				offsets = [offset[0], offset[1], orig[0], orig[1]]
	if offsets is not None:
		if len(offsets) < 4:
			raise ValueError(f"Atlas region '{region_name}' has invalid offsets: {offsets}")
		result.append(("offsets", offsets[:4]))

	rotate = by_key.get("rotate")
	if rotate is not None:
		normalized_rotate = normalize_rotate(rotate)
		if normalized_rotate is not None:
			result.append(("rotate", normalized_rotate))

	index = by_key.get("index")
	if index is not None and (not index or index[0].strip() != "-1"):
		result.append(("index", index))

	consumed = {"bounds", "offsets", "xy", "size", "orig", "offset", "rotate", "index"}
	for key, values in entries:
		if key not in consumed:
			result.append((key, values))

	return result


def convert_atlas_to_spine43(text: str) -> tuple[str, dict]:
	"""Convert legacy atlas metadata to the compact Spine 4.3 representation.

	The texture itself is not repacked. Coordinates and dimensions therefore stay
	exactly the same; only equivalent metadata fields are rewritten.
	"""
	atlas = parse_atlas(text)
	converted_pages: list[dict] = []

	for page in atlas["pages"]:
		regions = [
			{"name": region["name"], "entries": compact_region_entries(region["name"], region["entries"])}
			for region in page["regions"]
		]
		converted_pages.append({
			"name": page["name"],
			"entries": compact_page_entries(page["entries"]),
			"regions": regions,
		})

	converted = {"header": atlas["header"], "pages": converted_pages}
	lines: list[str] = []

	for key, values in converted["header"]:
		lines.append(f"{key}:{','.join(values)}")

	for page_index, page in enumerate(converted["pages"]):
		if lines and (page_index > 0 or converted["header"]):
			lines.append("")
		lines.append(page["name"])
		for key, values in page["entries"]:
			lines.append(f"{key}:{','.join(values)}")
		for region in page["regions"]:
			lines.append(region["name"])
			for key, values in region["entries"]:
				lines.append(f"{key}:{','.join(values)}")

	output = "\n".join(lines).lstrip("\r\n") + "\n"
	return output, converted


def validate_spine43_atlas_conversion(source_text: str, converted_text: str) -> tuple[int, int]:
	"""Verify the compact atlas preserves page/region geometry and texture names."""
	source = parse_atlas(source_text)
	converted = parse_atlas(converted_text)

	if len(source["pages"]) != len(converted["pages"]):
		raise ValueError("Atlas page count changed during Spine 4.3 conversion")

	region_count = 0
	for source_page, converted_page in zip(source["pages"], converted["pages"]):
		if source_page["name"] != converted_page["name"]:
			raise ValueError(
				f"Atlas page name changed from '{source_page['name']}' to '{converted_page['name']}'"
			)
		if len(source_page["regions"]) != len(converted_page["regions"]):
			raise ValueError(f"Atlas region count changed on page '{source_page['name']}'")

		for source_region, converted_region in zip(source_page["regions"], converted_page["regions"]):
			region_count += 1
			if source_region["name"] != converted_region["name"]:
				raise ValueError(
					f"Atlas region name changed from '{source_region['name']}' to '{converted_region['name']}'"
				)

			source_entries = atlas_entries_to_dict(source_region["entries"])
			converted_entries = atlas_entries_to_dict(converted_region["entries"])
			expected_bounds = source_entries.get("bounds")
			if expected_bounds is None:
				expected_bounds = source_entries.get("xy", [])[:2] + source_entries.get("size", [])[:2]
			if converted_entries.get("bounds") != expected_bounds:
				raise ValueError(f"Atlas bounds changed for region '{source_region['name']}'")

			legacy_keys = {"xy", "size", "orig", "offset"}
			if legacy_keys.intersection(converted_entries):
				raise ValueError(f"Legacy atlas fields remain in region '{source_region['name']}'")

	return len(converted["pages"]), region_count


def read_atlas_page_names(atlas_path: Path) -> list[str]:
	text = atlas_path.read_text(encoding="utf-8-sig")
	return [page["name"] for page in parse_atlas(text)["pages"]]


def atlas_texture_pairs(input_path: Path, output_path: Path) -> tuple[list[str], list[tuple[Path, Path]]]:
	pages = read_atlas_page_names(input_path)
	if not pages:
		raise ValueError(f"Atlas contains no texture page: {input_path}")

	pairs: list[tuple[Path, Path]] = []
	for page_name in pages:
		page_rel = Path(page_name.replace("\\", "/"))
		if page_rel.is_absolute() or ".." in page_rel.parts:
			raise ValueError(f"Unsafe atlas texture path '{page_name}' in {input_path}")

		texture_source = input_path.parent / page_rel
		if not texture_source.is_file():
			raise FileNotFoundError(
				f"Atlas texture does not exist: {texture_source} (referenced by {input_path.name})"
			)
		pairs.append((texture_source, output_path.parent / page_rel))

	return pages, pairs


def copy_atlas_textures_for_43(input_path: Path, output_path: Path) -> list[str]:
	pages, pairs = atlas_texture_pairs(input_path, output_path)
	for texture_source, texture_destination in pairs:
		texture_destination.parent.mkdir(parents=True, exist_ok=True)
		if texture_source.resolve() != texture_destination.resolve():
			shutil.copy2(texture_source, texture_destination)
		print(f"[texture-ready] {texture_destination}")
	return pages


def write_atlas_for_43(input_path: Path, output_path: Path, pages: list[str]) -> None:
	source_text = input_path.read_text(encoding="utf-8-sig")
	converted_text, converted = convert_atlas_to_spine43(source_text)
	page_count, region_count = validate_spine43_atlas_conversion(source_text, converted_text)

	converted_page_names = [page["name"] for page in converted["pages"]]
	if converted_page_names != pages:
		raise ValueError(
			f"Atlas page list changed during conversion: {pages!r} -> {converted_page_names!r}"
		)

	output_path.parent.mkdir(parents=True, exist_ok=True)
	# Use a non-atlas temporary file and atomically expose the completed result.
	temp_path = output_path.with_name(output_path.name + ".tmp")
	temp_path.write_text(converted_text, encoding="utf-8", newline="\n")

	raw = temp_path.read_bytes()
	expected = pages[0].encode("utf-8")
	if raw.startswith(b"\xef\xbb\xbf"):
		temp_path.unlink(missing_ok=True)
		raise ValueError(f"Output atlas still contains a UTF-8 BOM: {output_path}")
	if not raw.startswith(expected + b"\n") and raw != expected:
		preview = raw[:80].decode("utf-8", errors="replace").replace("\r", "\\r").replace("\n", "\\n")
		temp_path.unlink(missing_ok=True)
		raise ValueError(
			f"Output atlas does not start with '{pages[0]}'. First bytes: {preview!r}"
		)

	temp_path.replace(output_path)
	print(f"[atlas-4.3] {input_path} -> {output_path} ({page_count} page(s), {region_count} region(s))")


def process_files(args: argparse.Namespace) -> None:
	input_dir = Path(args.input_directory).resolve()
	output_dir = Path(args.output_directory).resolve()

	if not input_dir.exists() or not input_dir.is_dir():
		raise FileNotFoundError(f"Input directory does not exist: {input_dir}")

	output_dir.mkdir(parents=True, exist_ok=True)

	converter_exe = locate_executable("SpineSkeletonDataConverter.exe")
	is_43 = is_spine_43_target(args.version)
	atlas_exe = None if is_43 else locate_executable("SpineAtlasDowngrade.exe")

	files = [
		path for path in input_dir.rglob("*")
		if path.is_file() and path.suffix.lower() in {".json", ".skel", ".atlas"}
	]

	# For Spine 4.3, copy texture pages first, then atomically publish a compact
	# 4.3 atlas. The PNG is not repacked, so all source coordinates remain valid.
	atlas_pages: dict[Path, list[str]] = {}
	if is_43:
		for path in files:
			if path.suffix.lower() != ".atlas":
				continue
			relative_path = path.relative_to(input_dir)
			output_path = output_dir / relative_path
			atlas_pages[path] = copy_atlas_textures_for_43(path, output_path)

		for path, pages in atlas_pages.items():
			relative_path = path.relative_to(input_dir)
			write_atlas_for_43(path, output_dir / relative_path, pages)

	for path in files:
		suffix = path.suffix.lower()
		relative_path = path.relative_to(input_dir)
		destination_parent = (output_dir / relative_path).parent
		destination_parent.mkdir(parents=True, exist_ok=True)

		if suffix in {".json", ".skel"}:
			output_suffix = determine_output_suffix(suffix, args.format)
			destination_path = output_dir / relative_path.with_suffix(output_suffix)
			command = build_converter_command(
				converter_exe,
				path,
				destination_path,
				args.version,
				args.remove_curve,
			)
			print(f"[converter] {format_command(command)}")
			subprocess.run(command, check=True)
		elif not is_43:
			assert atlas_exe is not None
			command = build_atlas_command(atlas_exe, path, output_dir / relative_path.parent)
			print(f"[atlas] {format_command(command)}")
			subprocess.run(command, check=True)


def parse_arguments(argv: list[str]) -> argparse.Namespace:
	parser = argparse.ArgumentParser(description="Batch convert Spine assets using project tools")
	parser.add_argument("input_directory", help="Root directory containing source files")
	parser.add_argument("output_directory", help="Destination directory for converted files")
	parser.add_argument(
		"-v",
		"--version",
		help="Target Spine version (x.y.z). Defaults to source version.",
	)
	parser.add_argument(
		"--format",
		choices=["same", "json", "skel", "other"],
		default="same",
		help="Output format selection rule for .json/.skel files",
	)
	parser.add_argument(
		"--remove-curve",
		action="store_true",
		help="Strip animation curves instead of converting when crossing 3.x/4.x",
	)
	return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
	if argv is None:
		argv = sys.argv[1:]

	try:
		args = parse_arguments(argv)
		process_files(args)
	except FileNotFoundError as exc:
		print(f"Error: {exc}", file=sys.stderr)
		return 1
	except subprocess.CalledProcessError as exc:
		command_repr = format_command(list(exc.cmd)) if isinstance(exc.cmd, (list, tuple)) else str(exc.cmd)
		print(f"Subprocess failed with exit code {exc.returncode}: {command_repr}", file=sys.stderr)
		return exc.returncode or 1
	except ValueError as exc:
		print(f"Error: {exc}", file=sys.stderr)
		return 1

	return 0


if __name__ == "__main__":
	raise SystemExit(main())
