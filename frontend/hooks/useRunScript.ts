"use client";

import { useCallback, useRef, useState } from "react";
import { scripts, type ScriptEvent, type ScriptExecution, type ScriptRunResponse } from "@/lib/api";

export interface RunScriptOptions {
  onStatusChange?: (status: ScriptEvent["status"], execution_id: string) => void;
  onOutput?: (output: string) => void;
  onDone?: (execution: ScriptExecution) => void;
  onError?: (error: Error) => void;
}

export interface RunScriptResult {
  execution_id: string | null;
  status: ScriptEvent["status"] | null;
  output: string;
  error: Error | null;
  isRunning: boolean;
  run: (script_name: string) => Promise<ScriptRunResponse | null>;
  kill: () => Promise<void>;
  reset: () => void;
}

export function useRunScript(options: RunScriptOptions = {}): RunScriptResult {
  const [execution_id, setExecutionId] = useState<string | null>(null);
  const [status, setStatus] = useState<ScriptEvent["status"] | null>(null);
  const [output, setOutput] = useState("");
  const [error, setError] = useState<Error | null>(null);
  const [isRunning, setIsRunning] = useState(false);

  const eventSourceRef = useRef<EventSource | null>(null);
  const abortControllerRef = useRef<AbortController | null>(null);

  const cleanup = useCallback(() => {
    if (eventSourceRef.current) {
      eventSourceRef.current.close();
      eventSourceRef.current = null;
    }
    abortControllerRef.current = null;
  }, []);

  const run = useCallback(
    async (script_name: string): Promise<ScriptRunResponse | null> => {
      // Clean up any existing connection
      cleanup();

      setError(null);
      setOutput("");
      setIsRunning(true);

      try {
        const response = await scripts.run(script_name);
        const newExecutionId = response.execution_id;
        setExecutionId(newExecutionId);
        setStatus(response.status);

        // Set up SSE connection for live updates
        const es = scripts.events(newExecutionId);
        eventSourceRef.current = es;

        es.onmessage = (event) => {
          try {
            const data: ScriptEvent = JSON.parse(event.data);

            switch (data.type) {
              case "status":
                if (data.status) {
                  setStatus(data.status);
                  options.onStatusChange?.(data.status, data.execution_id);
                }
                break;

              case "output":
                if (data.output) {
                  setOutput(data.output);
                  options.onOutput?.(data.output);
                }
                break;

              case "done":
                setStatus(data.status ?? null);
                setIsRunning(false);
                // Fetch final execution details
                scripts.getStatus(data.execution_id).then((exec) => {
                  options.onDone?.(exec);
                }).catch(() => {});
                cleanup();
                break;

              case "error":
                setError(new Error(data.message ?? "Unknown error"));
                setIsRunning(false);
                options.onError?.(new Error(data.message ?? "Unknown error"));
                cleanup();
                break;
            }
          } catch (e) {
            console.error("Failed to parse SSE event:", e);
          }
        };

        es.onerror = () => {
          setError(new Error("Connection lost"));
          setIsRunning(false);
          cleanup();
        };

        return response;
      } catch (e) {
        setError(e instanceof Error ? e : new Error("Failed to start script"));
        setIsRunning(false);
        return null;
      }
    },
    [cleanup, options]
  );

  const kill = useCallback(async () => {
    if (!execution_id) return;

    try {
      await scripts.kill(execution_id);
      // The SSE will receive the 'killed' status event
    } catch (e) {
      setError(e instanceof Error ? e : new Error("Failed to kill script"));
    }
  }, [execution_id]);

  const reset = useCallback(() => {
    cleanup();
    setExecutionId(null);
    setStatus(null);
    setOutput("");
    setError(null);
    setIsRunning(false);
  }, [cleanup]);

  return {
    execution_id,
    status,
    output,
    error,
    isRunning,
    run,
    kill,
    reset,
  };
}