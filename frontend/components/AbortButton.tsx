"use client";

import { useRunScript } from "@/hooks/useRunScript";
import { scripts, type ScriptEvent } from "@/lib/api";

interface AbortButtonProps {
  execution_id: string | null;
  isRunning: boolean;
  onKill: () => Promise<void>;
  className?: string;
}

export function AbortButton({ execution_id, isRunning, onKill, className = "" }: AbortButtonProps) {
  if (!execution_id || !isRunning) {
    return null;
  }

  return (
    <button
      onClick={onKill}
      disabled={!isRunning}
      className={`btn btn-danger ${className}`}
      style={{
        display: "inline-flex",
        alignItems: "center",
        gap: "0.5rem",
        padding: "0.5rem 1rem",
        fontSize: "0.875rem",
        fontWeight: 500,
      }}
      aria-label="Abort script execution"
    >
      <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
        <rect x="2" y="2" width="20" height="20" rx="2" />
      </svg>
      Abort
    </button>
  );
}

interface ScriptRunnerProps {
  scriptName: string;
  label?: string;
  onComplete?: (output: string) => void;
  onError?: (error: Error) => void;
  children?: (props: {
    isRunning: boolean;
    status: ScriptEvent["status"] | null;
    output: string;
    error: Error | null;
    run: () => Promise<void>;
    abort: () => Promise<void>;
    reset: () => void;
  }) => React.ReactNode;
}

export function ScriptRunner({ scriptName, label, onComplete, onError, children }: ScriptRunnerProps) {
  const { execution_id, status, output, error, isRunning, run, kill, reset } = useRunScript({
    onDone: (exec) => {
      if (exec.status === "completed" && exec.output) {
        onComplete?.(exec.output);
      } else if (exec.status === "killed" || exec.status === "failed") {
        onError?.(new Error(exec.error ?? `Script ${exec.status}`));
      }
    },
    onError,
  });

  const handleRun = async () => {
    await run(scriptName);
  };

  const handleAbort = async () => {
    await kill();
  };

  if (children) {
    return children({
      isRunning,
      status,
      output,
      error,
      run: handleRun,
      abort: handleAbort,
      reset,
    });
  }

  return (
    <div className="script-runner" style={{ display: "flex", gap: "0.75rem", alignItems: "center", flexWrap: "wrap" }}>
      <button
        onClick={handleRun}
        disabled={isRunning}
        className="btn btn-primary"
        style={{ padding: "0.5rem 1rem" }}
      >
        {isRunning ? "Running..." : label ?? `Run ${scriptName}`}
      </button>

      <AbortButton execution_id={execution_id} isRunning={isRunning} onKill={handleAbort} />

      {status && (
        <span className="badge" style={{ textTransform: "capitalize" }}>
          {status}
        </span>
      )}

      {error && (
        <span style={{ color: "var(--c-danger)", fontSize: "0.875rem" }}>
          Error: {error.message}
        </span>
      )}
    </div>
  );
}