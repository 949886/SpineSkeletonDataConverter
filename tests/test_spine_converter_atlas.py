import importlib.util
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "tools" / "SpineConverter.py"
SPEC = importlib.util.spec_from_file_location("spine_converter", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
spine_converter = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(spine_converter)


class Spine43AtlasConversionTests(unittest.TestCase):
	def test_legacy_region_becomes_compact(self):
		source = """
page.png
size: 128, 64
format: RGBA8888
filter: Linear, Nearest
repeat: none
region
  rotate: true
  xy: 1, 2
  size: 3, 4
  orig: 5, 6
  offset: 1, 1
  index: -1
"""
		converted, _ = spine_converter.convert_atlas_to_spine43(source)
		self.assertEqual(
			converted,
			"""page.png
size:128,64
filter:Linear,Nearest
region
bounds:1,2,3,4
offsets:1,1,5,6
rotate:90
""",
		)
		self.assertEqual(spine_converter.validate_spine43_atlas_conversion(source, converted), (1, 1))

	def test_default_offsets_and_index_are_omitted(self):
		source = """page.png
size:32,32
filter:Linear,Linear
region
rotate:false
xy:7,8
size:9,10
orig:9,10
offset:0,0
index:-1
"""
		converted, _ = spine_converter.convert_atlas_to_spine43(source)
		self.assertIn("bounds:7,8,9,10\n", converted)
		self.assertNotIn("offsets:", converted)
		self.assertNotIn("rotate:", converted)
		self.assertNotIn("index:", converted)

	def test_multiple_pages_are_preserved(self):
		source = """
page1.png
size:16,16
filter:Linear,Linear
region1
xy:1,2
size:3,4
orig:3,4
offset:0,0
index:-1

page2.png
size:32,32
filter:Nearest,Nearest
repeat:x
region2
xy:5,6
size:7,8
orig:7,8
offset:0,0
index:3
"""
		converted, model = spine_converter.convert_atlas_to_spine43(source)
		self.assertEqual([page["name"] for page in model["pages"]], ["page1.png", "page2.png"])
		self.assertIn("\n\npage2.png\n", converted)
		self.assertIn("repeat:x\n", converted)
		self.assertIn("index:3\n", converted)
		self.assertEqual(spine_converter.validate_spine43_atlas_conversion(source, converted), (2, 2))

	def test_native_spine43_atlas_is_idempotent(self):
		source = """page.png
size:1825,300
filter:Linear,Linear
pma:true
regionA
bounds:265,33,25,38
regionB
bounds:1759,68,30,31
rotate:90
regionC
bounds:10,20,30,40
offsets:1,2,31,42
"""
		converted, _ = spine_converter.convert_atlas_to_spine43(source)
		self.assertEqual(converted, source)


if __name__ == "__main__":
	unittest.main()
