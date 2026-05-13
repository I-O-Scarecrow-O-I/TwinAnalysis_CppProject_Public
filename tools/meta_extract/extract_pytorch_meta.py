#!/usr/bin/env python3
"""Extract input/output metadata from a PyTorch TorchScript (.pt) model.

If static shape analysis yields dynamic axes (e.g. -1), the script can
receive an explicit --input-shape to overwrite the shapes.
When the shape remains dynamic, a configurable trial inference loop
prints concrete shapes to stderr without altering the output JSON.

Outputs a JSON object to stdout (and optionally to --out <file>).

Exit codes:
  0  success
  1  failure (JSON with status=FAILED still written to stdout)

  静态图分析保持优先。

新增 --input-shape 可选参数（多个整数）。

新增 --trial-shapes 可选参数（多个逗号分隔的形状字符串，用于自定义试探序列）。

流程：静态分析 → 若存在动态维度且提供了 --input-shape，则用该形状覆盖第一个输入（覆盖后不再动态） →
      若仍动态且未提供 --input-shape，则用可配置的试探序列尝试推导实际形状，结果仅输出到控制台，不修改 JSON 文件。
"""

import sys
import json
import argparse
import os


def _dtype_str(dtype):
    """Convert torch dtype to string like float32, int64, etc."""
    mapping = {
        "torch.float32": "float32",
        "torch.float":   "float32",
        "torch.float64": "float64",
        "torch.double":  "float64",
        "torch.float16": "float16",
        "torch.half":    "float16",
        "torch.int8":    "int8",
        "torch.int16":   "int16",
        "torch.short":   "int16",
        "torch.int32":   "int32",
        "torch.int":     "int32",
        "torch.int64":   "int64",
        "torch.long":    "int64",
        "torch.bool":    "bool",
    }
    s = str(dtype)
    return mapping.get(s, s.replace("torch.", ""))


def _extract_torchscript(model_path):
    """Static graph analysis on a TorchScript model.
    Returns (inputs, outputs, error_msg). shapes may contain -1.
    """
    import torch
    try:
        model = torch.jit.load(model_path, map_location="cpu")
    except Exception as e:
        return None, None, f"Failed to load as TorchScript: {e}"

    inputs = []
    outputs = []

    try:
        graph = model.graph
        torch._C._jit_pass_inline(graph)
        graph_inputs = list(graph.inputs())
        for i, inp in enumerate(graph_inputs):
            if i == 0 and inp.debugName() in ("self", "self.1"):
                continue
            t = inp.type()
            name = inp.debugName() or f"input_{i}"
            try:
                sizes = list(t.sizes()) if hasattr(t, "sizes") else [-1]
            except Exception:
                sizes = [-1]
            try:
                dtype = _dtype_str(t.scalarType().lower()) if hasattr(t, "scalarType") else "float32"
            except Exception:
                dtype = "float32"
            inputs.append({
                "name": name,
                "dataType": dtype,
                "shape": sizes,
                "rank": len(sizes),
            })

        graph_outputs = list(graph.outputs())
        for i, out in enumerate(graph_outputs):
            t = out.type()
            name = out.debugName() or f"output_{i}"
            try:
                sizes = list(t.sizes()) if hasattr(t, "sizes") else [-1]
            except Exception:
                sizes = [-1]
            try:
                dtype = _dtype_str(t.scalarType().lower()) if hasattr(t, "scalarType") else "float32"
            except Exception:
                dtype = "float32"
            outputs.append({
                "name": name,
                "dataType": dtype,
                "shape": sizes,
                "rank": len(sizes),
            })
    except Exception as e:
        sys.stderr.write(f"Warning: graph inspection failed ({e}), using placeholder I/O\n")
        inputs = [{"name": "input_0", "dataType": "float32", "shape": [-1], "rank": 1}]
        outputs = [{"name": "output_0", "dataType": "float32", "shape": [-1], "rank": 1}]

    if not inputs:
        inputs = [{"name": "input_0", "dataType": "float32", "shape": [-1], "rank": 1}]
    if not outputs:
        outputs = [{"name": "output_0", "dataType": "float32", "shape": [-1], "rank": 1}]

    return inputs, outputs, None


def parse_trial_shapes(raw_strings):
    """Convert a list of comma-separated strings to list of shape lists."""
    if not raw_strings:
        return None
    result = []
    for s in raw_strings:
        try:
            shape = [int(x) for x in s.split(",")]
            if any(d <= 0 for d in shape):
                sys.stderr.write(f"Ignoring invalid trial shape: {s}\n")
                continue
            result.append(shape)
        except ValueError:
            sys.stderr.write(f"Ignoring unparseable trial shape: {s}\n")
    return result if result else None


def main():
    parser = argparse.ArgumentParser(description="Extract PyTorch model metadata")
    parser.add_argument("model_path", help="Path to TorchScript .pt file (or .pth)")
    parser.add_argument("--input-shape", nargs="+", type=int, default=None,
                        help="Optional concrete input shape, e.g. 1 2 10000")
    parser.add_argument("--trial-shapes", nargs="+", type=str, default=None,
                        help="Optional space-separated comma-separated shapes for trial inference, "
                             "e.g. 1,2,100 1,2,1000")
    parser.add_argument("--out", default=None, help="Optional output JSON file path")
    args = parser.parse_args()

    model_path = args.model_path
    input_shape_cli = args.input_shape   # list of ints or None

    # Configure trial shapes
    trial_shapes_list = parse_trial_shapes(args.trial_shapes)
    if trial_shapes_list is None:
        trial_shapes_list = [
            [1, 2, 100],
            [1, 2, 1000],
            [1, 2, 10000],
            [2, 100],
        ]

    def fail(msg):
        out = {
            "status": "FAILED",
            "message": msg,
            "framework": "PyTorch",
            "modelName": os.path.splitext(os.path.basename(model_path))[0],
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

    # 1. Static analysis
    inputs, outputs, err = _extract_torchscript(model_path)
    if err:
        fail(err)

    # 2. Helper to detect dynamic shapes
    def has_dynamic(io_list):
        for item in io_list:
            if -1 in item["shape"]:
                return True
        return False

    dynamic_present = has_dynamic(inputs) or has_dynamic(outputs)

    # 3. If --input-shape provided, resolve both input and output shapes
    if input_shape_cli is not None:
        # Override the first input shape
        if inputs:
            inputs[0]["shape"] = list(input_shape_cli)
            inputs[0]["rank"] = len(input_shape_cli)

        # Run forward to get concrete output shapes
        try:
            import torch
            model = torch.jit.load(model_path, map_location="cpu")
            model.eval()
            dummy = torch.randn(input_shape_cli)
            with torch.no_grad():
                out = model(dummy)
            if isinstance(out, torch.Tensor):
                out_shapes = [list(out.shape)]
            else:
                out_shapes = [list(o.shape) for o in out]

            # Rebuild outputs based on actual results
            new_outputs = []
            for i, shape in enumerate(out_shapes):
                name = f"output_{i}"
                # Try to preserve original name if available
                if i < len(outputs):
                    name = outputs[i].get("name", name)
                dtype_str = "float32"   # default
                if isinstance(out, torch.Tensor):
                    dtype_str = _dtype_str(out.dtype)
                elif isinstance(out, (list, tuple)) and i < len(out):
                    dtype_str = _dtype_str(out[i].dtype)
                new_outputs.append({
                    "name": name,
                    "dataType": dtype_str,
                    "shape": shape,
                    "rank": len(shape),
                })
            outputs = new_outputs
            dynamic_present = False
            sys.stderr.write(f"Info: Resolved output shapes using provided input shape: {out_shapes}\n")
        except Exception as e:
            sys.stderr.write(f"Warning: Failed to infer output shapes with provided input shape: {e}\n")

    # 4. If still dynamic and no CLI shape, run trial inference (results to stderr only)
    if dynamic_present and input_shape_cli is None:
        sys.stderr.write("Info: detected dynamic shapes, attempting trial inference...\n")
        import torch
        try:
            model = torch.jit.load(model_path, map_location="cpu")
            model.eval()
            discovered = False
            for shape in trial_shapes_list:
                try:
                    dummy = torch.randn(shape)
                    with torch.no_grad():
                        out = model(dummy)
                    if isinstance(out, torch.Tensor):
                        out_shapes = [list(out.shape)]
                    else:
                        out_shapes = [list(o.shape) for o in out]
                    sys.stderr.write(
                        f"Trial with shape {shape} succeeded. "
                        f"Input shape: {list(dummy.shape)}, Output shape(s): {out_shapes}\n"
                    )
                    discovered = True
                    break
                except Exception as e:
                    sys.stderr.write(f"Trial with shape {shape} failed: {e}\n")
            if not discovered:
                sys.stderr.write("Warning: trial inference failed to discover concrete shapes.\n")
        except Exception as e:
            sys.stderr.write(f"Warning: trial inference could not be performed: {e}\n")

    # 5. Build output JSON
    model_name = os.path.splitext(os.path.basename(model_path))[0]
    out = {
        "status": "OK",
        "framework": "PyTorch",
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
