# TensorFlow Model Samples

This directory holds TensorFlow SavedModel directories (and `.h5`/`.keras` files)
for testing `AiModelPackagingCLI` with `implementation.type = "TensorFlow"`.

## How to create a dummy affine model

Run the following from the repository root (requires TensorFlow ≥ 2.0):

```bash
python3 tools/create_dummy_models.py --tf
```

This creates `tf_models/affine_saved_model/` — a TensorFlow 2 SavedModel
with a single linear layer that maps `[None, 4] float32` → `[None, 4] float32`.

## Running the CLI

```bash
# Set the TensorFlow Python executable
export TF_PYTHON=python3     # or the path to your venv/conda Python with tensorflow installed

# Run packaging (SavedModel directory)
./AiModelPackagingCLI \
  --request requests/request_tensorflow_affine.json \
  --model   tf_models/affine_saved_model \
  --out     packageResults
```

## Supported model formats (Linux)

| Format | Extension | Notes |
|--------|-----------|-------|
| SavedModel | directory | Primary supported format |
| Keras HDF5 | `.h5` | Loaded via `tf.keras.models.load_model` |
| Keras | `.keras` | Loaded via `tf.keras.models.load_model` |

## Notes

- If `TF_PYTHON` is not set, the package will be produced with `status = "FAILED"`.
- The generated C++ wrapper stub **compiles** but does **not** implement inference.
  See `Warnings.json` for the `INFERENCE_NOT_IMPLEMENTED` warning in the output package.
