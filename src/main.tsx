import React from 'react'
import { createRoot } from 'react-dom/client'
import App from '../App'
import './index.css'

function Main() {
  return <App />
}

const container = document.getElementById('root')!
const root = createRoot(container)
root.render(
  <React.StrictMode>
    <Main />
  </React.StrictMode>
)
