"""Regressions for translation fallback, stable links and stale-document detection."""
import copy
from pathlib import Path
import tempfile
import unittest

import generate


class ReferenceTests(unittest.TestCase):
    def setUp(self):
        self.catalog = {"schema": 1, "version": "1.1.1", "tasks": [{
            "key": "demo", "label": "Descriptor method", "outputs": ["series"],
            "parameters": [{"key": "factor", "label": "Descriptor parameter",
                            "help": "Descriptor explanation", "default": 2,
                            "choices": ["age", "depth"], "numeric": True,
                            "minimum": 0.000123456789, "maximum": 1.7976931348623157e308,
                            "visibleWhen": {"mode": "fixed"}}]}]}
        self.catalogs = {
            "zh": {"method.demo": "示例", "methodhelp.demo": "中文方法说明",
                   "parameter.factor": "系数", "parameterhelp.factor": "中文参数说明"},
            "en": {"method.demo": "Example", "methodhelp.demo": "English method explanation",
                   "parameter.factor": "Factor", "parameterhelp.factor": "English parameter explanation"}}

    def test_language_separation_preserves_values_and_anchors(self):
        zh = generate.render(self.catalog, self.catalogs, "zh")
        en = generate.render(self.catalog, self.catalogs, "en")
        self.assertIn("中文参数说明", zh)
        self.assertNotIn("English parameter explanation", zh)
        self.assertIn("English parameter explanation", en)
        self.assertNotIn("中文参数说明", en)
        for page in (zh, en):
            self.assertIn('<a id="m-demo-p-factor"></a>', page)
            self.assertIn("` age `, ` depth `", page)
            self.assertIn("0.000123456789", page)
            self.assertNotIn("1.797693", page)
            self.assertIn("` mode ` = ` fixed `", page)

    def test_choices_show_ui_labels_without_changing_saved_identifiers(self):
        parameter = self.catalog["tasks"][0]["parameters"][0]
        parameter["default"] = "age"
        self.catalogs["zh"]["choice.factor.age"] = "年龄"
        self.catalogs["en"]["choice.factor.age"] = "Age"
        self.catalogs["en"]["choice.factor.depth"] = "Depth [X]"
        zh = generate.render(self.catalog, self.catalogs, "zh")
        en = generate.render(self.catalog, self.catalogs, "en")
        self.assertIn("默认值: 年龄 (` age `)", zh)
        self.assertIn("可选值: 年龄 (` age `), Depth \\[X\\] (` depth `)", zh)
        self.assertIn("Default: Age (` age `)", en)
        self.assertNotIn("年龄", en)
        self.assertEqual(parameter["default"], "age")
        self.assertEqual(parameter["choices"], ["age", "depth"])

    def test_missing_or_empty_chinese_uses_english_once(self):
        del self.catalogs["zh"]["parameter.factor"]
        self.catalogs["zh"]["parameterhelp.factor"] = ""
        page = generate.render(self.catalog, self.catalogs, "zh")
        self.assertIn("### Factor", page)
        self.assertEqual(page.count("English parameter explanation"), 1)
        self.assertNotIn("parameterhelp.factor", page)

    def test_specific_english_precedes_wrong_generic_chinese_meaning(self):
        self.catalogs["en"]["parameterhelp.demo.factor"] = "Task-specific meaning"
        page = generate.render(self.catalog, self.catalogs, "zh")
        self.assertIn("Task-specific meaning", page)
        self.assertNotIn("中文参数说明", page)

    def test_descriptor_fallback_without_any_translations(self):
        page = generate.render(self.catalog, {"zh": {}, "en": {}}, "zh")
        self.assertIn("### Descriptor parameter", page)
        self.assertIn("Descriptor explanation", page)
        self.assertNotIn("parameterhelp.", page)

    def test_check_is_read_only_and_detects_changed_defaults_and_translations(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            pages = {lang: generate.render(self.catalog, self.catalogs, lang) for lang in ("zh", "en")}
            self.assertEqual(len(generate.synchronize(root, pages, check=False)), 2)
            path = root / generate.PATHS["zh"]
            path.write_bytes(path.read_bytes().replace(b"\n", b"\r\n"))
            self.assertEqual(generate.synchronize(root, pages, check=True), [])
            before = {lang: (root / generate.PATHS[lang]).read_bytes() for lang in pages}
            self.catalog["tasks"][0]["parameters"][0]["default"] = 3
            pages = {lang: generate.render(self.catalog, self.catalogs, lang) for lang in pages}
            self.assertEqual(len(generate.synchronize(root, pages, check=True)), 2)
            for lang in pages:
                self.assertEqual((root / generate.PATHS[lang]).read_bytes(), before[lang])
            generate.synchronize(root, pages, check=False)
            self.catalogs["zh"]["parameterhelp.factor"] = "新说明"
            pages["zh"] = generate.render(self.catalog, self.catalogs, "zh")
            self.assertEqual(generate.synchronize(root, pages, check=True), [generate.PATHS["zh"]])

    def test_stale_binary_and_duplicate_anchors_are_rejected(self):
        generate.validate(self.catalog, "1.1.1")
        with self.assertRaises(ValueError):
            generate.validate(self.catalog, "1.1.2")
        self.catalog["tasks"][0]["parameters"].append(copy.deepcopy(self.catalog["tasks"][0]["parameters"][0]))
        with self.assertRaises(ValueError):
            generate.validate(self.catalog, "1.1.1")


if __name__ == "__main__":
    unittest.main()
