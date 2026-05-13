#!/usr/bin/env python3
"""
Create small dummy TorchScript and TensorFlow SavedModel files for testing
AiModelPackagingCLI TF/PT support.

Usage:
    # Create PyTorch dummy model (requires torch>=2.0):
    python3 tools/create_dummy_models.py --torch

    # Create TensorFlow dummy model (requires tensorflow>=2.0):
    python3 tools/create_dummy_models.py --tf

    # Create both:
    python3 tools/create_dummy_models.py --torch --tf
"""

import argparse
import os
import sys


def create_pytorch_affine(out_path: str):
    """Create a tiny affine (linear) TorchScript model: y = Wx + b, shape [1,4]->[1,4]."""
    import torch
    import torch.nn as nn

    class AffineModel(nn.Module):
        def __init__(self):
            super().__init__()
            self.fc = nn.Linear(4, 4)

        def forward(self, x: torch.Tensor) -> torch.Tensor:
            return self.fc(x)

    model = AffineModel()
    model.eval()

    # Trace the model with a dummy input
    dummy_input = torch.randn(1, 4)
    scripted = torch.jit.trace(model, dummy_input)

    os.makedirs(os.path.dirname(out_path) if os.path.dirname(out_path) else ".", exist_ok=True)
    scripted.save(out_path)
    print(f"Created PyTorch TorchScript model: {out_path}")


def create_tensorflow_affine(out_dir: str):
    """Create a tiny TensorFlow 2 SavedModel: y = Wx + b, shape [1,4]->[1,4]."""
    import tensorflow as tf

    class AffineModel(tf.Module):
        def __init__(self):
            super().__init__()
            self.W = tf.Variable(tf.random.normal([4, 4]), name="W")
            self.b = tf.Variable(tf.zeros([4]), name="b")

        @tf.function(input_signature=[tf.TensorSpec(shape=[None, 4], dtype=tf.float32, name="input_1")])
        def __call__(self, x):
            return tf.matmul(x, self.W) + self.b

    model = AffineModel()
    os.makedirs(out_dir, exist_ok=True)
    tf.saved_model.save(model, out_dir)
    print(f"Created TensorFlow SavedModel: {out_dir}")


def main():
    parser = argparse.ArgumentParser(description="Create dummy TF/PT models for testing")
    parser.add_argument("--torch", action="store_true", help="Create PyTorch TorchScript dummy model")
    parser.add_argument("--tf",    action="store_true", help="Create TensorFlow SavedModel dummy model")
    parser.add_argument("--pt-out",  default="pt_models/affine_ts.pt",             help="Output path for PyTorch model")
    parser.add_argument("--tf-out",  default="tf_models/affine_saved_model",       help="Output directory for TF model")
    args = parser.parse_args()

    if not args.torch and not args.tf:
        parser.print_help()
        sys.exit(0)

    if args.torch:
        create_pytorch_affine(args.pt_out)

    if args.tf:
        create_tensorflow_affine(args.tf_out)


if __name__ == "__main__":
    main()
