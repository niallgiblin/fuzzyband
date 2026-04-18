"""Compatibility shims for TensorFlow Datasets with protobuf 6+ (Python 3.13 / TF 2.21).

TFDS 4.9.9 compares ``field.label == field.LABEL_REPEATED`` in
``DatasetInfo.read_from_directory``. The upb-backed ``FieldDescriptor`` in protobuf 6+
does not expose ``.label``; ``is_repeated`` is the supported replacement.

See: https://github.com/protocolbuffers/protobuf/commit/0a8ff55518ea5874478ad5b26515b31d186045a9
"""

from __future__ import annotations

import inspect
import textwrap


def apply_tfds_protobuf_patch() -> None:
    """Monkey-patch ``DatasetInfo.read_from_directory`` once (idempotent)."""
    import tensorflow_datasets.core.dataset_info as di

    fn = di.DatasetInfo.read_from_directory
    if getattr(fn, "_tfds_protobuf_patch_applied", False):
        return

    src = inspect.getsource(fn)
    needle = "elif field.label == field.LABEL_REPEATED:"
    if needle not in src:
        return

    src = src.replace(
        needle,
        'elif getattr(field, "is_repeated", False):',
    )
    dedented = textwrap.dedent(src)
    exec(dedented, di.__dict__)
    new_fn = di.read_from_directory
    new_fn._tfds_protobuf_patch_applied = True  # type: ignore[attr-defined]
    di.DatasetInfo.read_from_directory = new_fn
    del di.read_from_directory
