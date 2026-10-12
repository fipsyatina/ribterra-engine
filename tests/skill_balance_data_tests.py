from pathlib import Path
import copy
import json
import sys
import unittest

root = Path(sys.argv.pop(1))
sys.path.insert(0, str(root/'tools'))
from compile_skill_balance import header, validated

class SkillBalanceTests(unittest.TestCase):
    def setUp(self):
        self.data = json.loads((root/'data/skills.json').read_text(encoding='utf-8'))

    def test_canonical_build_and_balance_changes(self):
        rendered = header(self.data)
        reordered = dict(reversed(list(self.data.items())))
        self.assertEqual(rendered, header(reordered))
        changed = copy.deepcopy(self.data)
        changed['experience_curve'][1][1] += 1
        self.assertNotEqual(rendered, header(changed))
        self.assertIn('canonical_json', rendered)

    def test_rejects_invalid_author_bounds_and_curves(self):
        changes = [
            ('skill_cap_bp', 1501), ('familiarity_cap_bp', 201), ('work_units_per_xp', True),
            ('ticks_per_day', 0), ('ticks_per_day', 1201), ('unknown', 1),
            ('experience_curve', [[0,-800],[0,100]]),
            ('experience_curve', [[0,-800],[10,-900]]),
            ('experience_curve', [[1,-800],[10,100]]),
            ('experience_curve', [[0,-800],[10,1501]]),
            ('experience_curve', [[False,-800],[10,100]]),
            ('familiarity_recovery_day_denominator', 7),
            ('familiarity_recovery_day_numerator', 1_000_000),
        ]
        for key, value in changes:
            data = copy.deepcopy(self.data)
            data[key] = value
            with self.subTest(key=key,value=value), self.assertRaises(ValueError):
                validated(data)

if __name__ == '__main__':
    unittest.main()
