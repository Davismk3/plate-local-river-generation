import os
import sys
if __package__ is None or __package__ == "":
    sys.path.append(os.path.dirname(os.path.dirname(__file__)))
from app.visualizer import runConfiguredVisualizer


def main():
    runConfiguredVisualizer()


if __name__ == "__main__":
    main()
