"""The benchmark must detect late corruption and compare arbitrary row groups."""
from pathlib import Path
import sys
import tempfile
import unittest

import pyarrow as pa
import pyarrow.parquet as pq

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from performance import exact_parquet


class PerformanceChecks(unittest.TestCase):
    def test_comparison_handles_different_row_groups_and_nullable_timestamps(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            values = [None if i % 17 == 0 else 1_612_517_400_000_000_000 + i for i in range(150003)]
            table = pa.table({"timestamp": pa.array(values, type=pa.int64()), "id": range(len(values))})
            pq.write_table(table, root / "a.parquet", row_group_size=17003)
            pq.write_table(table, root / "b.parquet", row_group_size=80000)
            self.assertTrue(exact_parquet(root / "a.parquet", root / "b.parquet"))
            values[-2] += 1
            corrupt = pa.table({"timestamp": pa.array(values, type=pa.int64()), "id": range(len(values))})
            pq.write_table(corrupt, root / "b.parquet", row_group_size=80000)
            self.assertFalse(exact_parquet(root / "a.parquet", root / "b.parquet"))


if __name__ == "__main__":
    unittest.main()
