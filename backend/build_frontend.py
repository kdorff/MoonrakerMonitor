Import("env")
import subprocess
import os

def build_frontend(source, target, env):
    print("Building frontend...")
    # Get the project directory (backend)
    project_dir = env.get("PROJECT_DIR")
    # Path to frontend directory
    frontend_dir = os.path.join(project_dir, "..", "frontend")
    
    # Run npm run build
    # Use shell=True for Windows compatibility if needed, but on Mac/Linux it's usually fine
    # We use subprocess.run for a clean execution
    try:
        subprocess.run(["npm", "run", "build"], cwd=frontend_dir, check=True)
        print("Frontend build successful!")
    except Exception as e:
        print(f"Error building frontend: {e}")
        # We might want to stop the build if frontend fails
        # env.Exit(1) 

# Add pre-action for buildfs
# We run it immediately if buildfs or uploadfs is in the targets to ensure 
# the data folder is updated before the filesystem image is packed.
if "buildfs" in COMMAND_LINE_TARGETS or "uploadfs" in COMMAND_LINE_TARGETS:
    build_frontend(None, None, env)

