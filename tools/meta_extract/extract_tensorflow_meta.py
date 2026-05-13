#!/usr/bin/env python3
"""Extract input/output metadata from a TensorFlow SavedModel or .h5/.keras file.

Outputs a JSON object to stdout (and optionally to --out <file>).

Exit codes:
  0  success
  1  failure (JSON with status=FAILED still written to stdout)
"""

import sys
import json
import argparse
import os


def _dtype_str(dtype):
    """Normalise TensorFlow dtype to canonical string like float32, int64, etc."""
    try:
        name = dtype.name  # e.g. 'float32', 'int64', 'bool'
        return name
    except AttributeError:
        return str(dtype)


def _shape_list(shape):
    """Convert a TensorShape to a plain list (None becomes -1)."""
    try:
        return [d if d is not None else -1 for d in shape.as_list()]
    except Exception:
        return []


def _extract_from_saved_model(model_path):
    import tensorflow as tf  # type: ignore
    loaded = tf.saved_model.load(model_path)

    result_inputs = []
    result_outputs = []

    # Try the default serving signature first
    sigs = loaded.signatures
    sig_key = "serving_default"
    if sig_key not in sigs and sigs:
        sig_key = next(iter(sigs))

    if sigs and sig_key in sigs:
        sig = sigs[sig_key]
        for name, spec in sig.structured_input_signature[1].items():
            shape = _shape_list(spec.shape)
            result_inputs.append({
                "name": name,
                "dataType": _dtype_str(spec.dtype),
                "shape": shape,
                "rank": len(shape),
            })
        for name, tensor in sig.structured_outputs.items():
            shape = _shape_list(tensor.shape)
            result_outputs.append({
                "name": name,
                "dataType": _dtype_str(tensor.dtype),
                "shape": shape,
                "rank": len(shape),
            })
    else:
        # Fall back: try keras model
        try:
            model = tf.keras.models.load_model(model_path)
            for inp in model.inputs:
                shape = _shape_list(inp.shape)
                result_inputs.append({
                    "name": inp.name,
                    "dataType": _dtype_str(inp.dtype),
                    "shape": shape,
                    "rank": len(shape),
                })
            for out in model.outputs:
                shape = _shape_list(out.shape)
                result_outputs.append({
                    "name": out.name,
                    "dataType": _dtype_str(out.dtype),
                    "shape": shape,
                    "rank": len(shape),
                })
        except Exception as e:
            return None, f"Cannot extract via signature or keras: {e}"

    return result_inputs, result_outputs, None


def _extract_from_h5_or_keras(model_path):
    import tensorflow as tf  # type: ignore
    model = tf.keras.models.load_model(model_path)
    result_inputs = []
    result_outputs = []
    for inp in model.inputs:
        shape = _shape_list(inp.shape)
        result_inputs.append({
            "name": inp.name,
            "dataType": _dtype_str(inp.dtype),
            "shape": shape,
            "rank": len(shape),
        })
    for out in model.outputs:
        shape = _shape_list(out.shape)
        result_outputs.append({
            "name": out.name,
            "dataType": _dtype_str(out.dtype),
            "shape": shape,
            "rank": len(shape),
        })
    return result_inputs, result_outputs, None


def main():
    parser = argparse.ArgumentParser(description="Extract TensorFlow model metadata")
    parser.add_argument("model_path", help="Path to SavedModel directory or .h5/.keras file")
    parser.add_argument("--out", default=None, help="Optional output JSON file path")
    args = parser.parse_args()

    model_path = args.model_path

    def fail(msg):
        out = {
            "status": "FAILED",
            "message": msg,
            "framework": "TensorFlow",
            "modelName": os.path.basename(model_path.rstrip("/\\")),
            "inputs": [],
            "outputs": [],
        }
        text = json.dumps(out, indent=2)
        print(text)
        if args.out:
            try:
                with open(args.out, "w", encoding="utf-8") as f:
                    f.write(text)
            except Exception:
                pass
        sys.exit(1)

    if not os.path.exists(model_path):
        fail(f"Model path does not exist: {model_path}")

    try:
        ext = os.path.splitext(model_path)[1].lower()
        if os.path.isdir(model_path):
            result = _extract_from_saved_model(model_path)
            if len(result) == 3:
                inputs, outputs, err = result
            else:
                inputs, err = result
                outputs = []
            if err:
                fail(err)
        elif ext in (".h5", ".keras"):
            inputs, outputs, err = _extract_from_h5_or_keras(model_path)
            if err:
                fail(err)
        else:
            # Try SavedModel directory style first, then h5
            try:
                result = _extract_from_saved_model(model_path)
                if len(result) == 3:
                    inputs, outputs, err = result
                else:
                    inputs, err = result
                    outputs = []
                if err:
                    fail(err)
            except Exception as e:
                fail(f"Unrecognised model format or load error: {e}")
    except Exception as e:
        fail(f"Unexpected error: {e}")

    model_name = os.path.basename(model_path.rstrip("/\\"))
    if not model_name:
        model_name = os.path.basename(os.path.dirname(model_path))

    out = {
        "status": "OK",
        "framework": "TensorFlow",
        "modelName": model_name,
        "inputs": inputs,
        "outputs": outputs,
    }
    text = json.dumps(out, indent=2)
    print(text)
    if args.out:
        try:
            with open(args.out, "w", encoding="utf-8") as f:
                f.write(text)
        except Exception as e:
            print(f"Warning: could not write --out file: {e}", file=sys.stderr)
    sys.exit(0)


if __name__ == "__main__":
    main()
